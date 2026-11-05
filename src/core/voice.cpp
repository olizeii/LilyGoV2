#include "voice.h"

#include <driver/i2s.h>
#include <mbedtls/base64.h>

#include "ble_manager.h"
#include "config/pins.h"

namespace voice {
namespace {

constexpr i2s_port_t PORT        = I2S_NUM_0;  // PDM-RX gibt es nur auf I2S0
constexpr size_t BLOCK_SAMPLES   = 320;        // 20 ms
constexpr size_t MAX_RAW         = 384;        // Bytes ADPCM-Block inkl. Kopf
constexpr size_t HEADER          = 4;

TaskHandle_t s_task       = nullptr;
volatile bool s_run       = false;
volatile bool s_timedOut  = false;
volatile float s_level    = 0;
uint32_t s_maxMs          = 12000;
size_t s_raw              = 0;   // Blockgröße für die aktuelle MTU

// --- IMA-ADPCM ----------------------------------------------------------------
const int8_t IDX_TAB[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int16_t STEP_TAB[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct Adpcm {
    int32_t pred = 0;
    int32_t idx  = 0;

    uint8_t encode(int16_t sample) {
        int32_t step = STEP_TAB[idx];
        int32_t diff = sample - pred;
        uint8_t nib  = 0;
        if (diff < 0) { nib = 8; diff = -diff; }
        int32_t vp = step >> 3;
        if (diff >= step) { nib |= 4; diff -= step; vp += step; }
        step >>= 1;
        if (diff >= step) { nib |= 2; diff -= step; vp += step; }
        step >>= 1;
        if (diff >= step) { nib |= 1; vp += step; }
        pred += (nib & 8) ? -vp : vp;
        if (pred > 32767) pred = 32767;
        if (pred < -32768) pred = -32768;
        idx += IDX_TAB[nib];
        if (idx < 0) idx = 0;
        if (idx > 88) idx = 88;
        return nib;
    }
};

// --- Block-Puffer ------------------------------------------------------------------
uint8_t s_buf[MAX_RAW];
size_t s_len      = 0;   // Bytes inkl. Kopf
bool s_highNibble = false;
uint8_t s_seq     = 0;
Adpcm s_enc;

void beginBlock() {
    s_buf[0] = s_seq++;
    s_buf[1] = uint8_t(s_enc.pred & 0xFF);
    s_buf[2] = uint8_t((s_enc.pred >> 8) & 0xFF);
    s_buf[3] = uint8_t(s_enc.idx);
    s_len = HEADER;
    s_highNibble = false;
}

void flushBlock() {
    if (s_len <= HEADER) return;
    if (s_highNibble) s_len++;  // halbes Byte abschließen
    unsigned char b64[MAX_RAW / 3 * 4 + 8];
    size_t out = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64) - 1, &out, s_buf, s_len) == 0) {
        b64[out] = 0;
        BleManager::instance().send("AUD", String(reinterpret_cast<const char*>(b64)));
    }
    beginBlock();
}

void push(int16_t sample) {
    uint8_t nib = s_enc.encode(sample);
    if (!s_highNibble) {
        s_buf[s_len] = nib;
        s_highNibble = true;
    } else {
        s_buf[s_len++] |= uint8_t(nib << 4);
        s_highNibble = false;
        if (s_len >= s_raw) flushBlock();
    }
}

bool installI2s() {
    i2s_config_t cfg = {};
    cfg.mode                 = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate          = SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 6;
    cfg.dma_buf_len          = BLOCK_SAMPLES;
    if (i2s_driver_install(PORT, &cfg, 0, nullptr) != ESP_OK) return false;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_PIN_NO_CHANGE;
    pins.ws_io_num    = PIN_MIC_CLK;   // PDM-Takt liegt auf WS
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = PIN_MIC_DATA;
    if (i2s_set_pin(PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(PORT);
        return false;
    }
    i2s_zero_dma_buffer(PORT);
    return true;
}

void task(void*) {
    static int16_t pcm[BLOCK_SAMPLES];
    float prevX = 0, prevY = 0;   // Gleichanteil-Filter
    float peak  = 4000;           // Spitzenwert-Verfolgung für die Verstärkung
    float gain  = 4;
    float lvl   = 0;
    uint32_t startMs = millis();
    uint32_t warmup  = 6;         // erste ~120 ms verwerfen (Einschwingen des PDM-Filters)

    s_enc = Adpcm();
    s_seq = 0;
    beginBlock();

    while (s_run) {
        if (millis() - startMs > s_maxMs) {
            s_timedOut = true;
            break;
        }
        size_t got = 0;
        if (i2s_read(PORT, pcm, sizeof(pcm), &got, pdMS_TO_TICKS(100)) != ESP_OK || !got) continue;
        size_t n = got / sizeof(int16_t);
        if (warmup) { warmup--; continue; }

        // Hochpass + Spitzenwert
        float blockPeak = 0;
        static float hp[BLOCK_SAMPLES];
        for (size_t i = 0; i < n; i++) {
            float x = pcm[i];
            float y = x - prevX + 0.995f * prevY;
            prevX = x;
            prevY = y;
            hp[i] = y;
            float a = fabsf(y);
            if (a > blockPeak) blockPeak = a;
        }
        peak = blockPeak > peak ? blockPeak : peak * 0.97f + blockPeak * 0.03f;
        if (peak < 1500) peak = 1500;
        float target = 18000.0f / peak;
        if (target > 10) target = 10;
        if (target < 1) target = 1;
        gain += (target < gain) ? (target - gain) : (target - gain) * 0.05f;  // runter sofort, rauf langsam

        double sum = 0;
        for (size_t i = 0; i < n; i++) {
            float v = hp[i] * gain;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            sum += double(v) * v;
            push(int16_t(v));
        }
        float rms = sqrtf(float(sum / n));
        float l   = rms / 5000.0f;
        if (l > 1) l = 1;
        lvl += (l > lvl ? 0.5f : 0.15f) * (l - lvl);   // schnell an, langsam aus
        s_level = lvl;
    }

    flushBlock();
    BleManager::instance().send("AUD_END");
    i2s_driver_uninstall(PORT);
    BleManager::instance().setFastLink(false);
    s_level = 0;
    s_run   = false;
    s_task  = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool start(uint32_t maxMs) {
    // Läuft noch ein alter Task aus? kurz warten
    for (int i = 0; i < 40 && s_task; i++) delay(5);
    if (s_task) return false;

    BleManager& ble = BleManager::instance();
    size_t usable = ble.payloadSize();
    if (usable < 4 + 80) {
        log_w("MTU zu klein fürs Audio (%u)", unsigned(usable));
        return false;
    }
    usable -= 4;  // "AUD|"
    s_raw = (usable / 4) * 3;
    if (s_raw > MAX_RAW) s_raw = MAX_RAW;

    if (!installI2s()) {
        log_w("I2S-PDM konnte nicht starten");
        return false;
    }
    ble.setFastLink(true);
    s_maxMs    = maxMs;
    s_timedOut = false;
    s_level    = 0;
    s_run      = true;
    if (xTaskCreatePinnedToCore(task, "voice", 6144, nullptr, 5, &s_task, 0) != pdPASS) {
        s_run = false;
        i2s_driver_uninstall(PORT);
        ble.setFastLink(false);
        return false;
    }
    return true;
}

void stop() { s_run = false; }

bool micOpen() {
    if (s_task) return false;   // Sprachassistent nimmt gerade auf
    return installI2s();
}

size_t micRead(int16_t* buf, size_t samples, uint32_t timeoutMs) {
    size_t got = 0;
    if (i2s_read(PORT, buf, samples * sizeof(int16_t), &got, pdMS_TO_TICKS(timeoutMs)) != ESP_OK) return 0;
    return got / sizeof(int16_t);
}

void micClose() { i2s_driver_uninstall(PORT); }

bool isRunning() { return s_task != nullptr; }

bool timedOut() { return s_timedOut; }

float level() { return s_level; }

}  // namespace voice
