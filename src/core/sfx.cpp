#include "sfx.h"

#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "config/pins.h"
#include "hal/power.h"

namespace sfx {
namespace {

constexpr i2s_port_t SPK   = I2S_NUM_1;
constexpr uint32_t RATE    = 16000;
constexpr size_t BLK       = 256;

int16_t* s_whip = nullptr;
uint32_t s_whipLen = 0;
int16_t* s_gun = nullptr;
uint32_t s_gunLen = 0;

TaskHandle_t s_task = nullptr;
volatile bool s_open = false;
volatile int s_pending = -1;   // Sound, der als Nächstes kommt

uint32_t s_rng = 0x12345678;
float noise() {   // weißes Rauschen -1..1
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (int32_t)s_rng / 2147483648.0f;
}

int16_t clip(float v) {
    v = tanhf(v * 1.4f) / tanhf(1.4f);   // weich begrenzen -> laut, ohne hässliches Kratzen
    return (int16_t)(v * 32000.0f);
}

// Peitsche: kurzes Zischen (Seil), dann ein sehr harter, heller Knall mit kurzem Nachhall
void makeWhip() {
    const uint32_t whoosh = RATE * 0.09f, total = RATE * 0.42f;
    s_whipLen = total;
    s_whip = static_cast<int16_t*>(heap_caps_malloc(total * 2, MALLOC_CAP_SPIRAM));
    if (!s_whip) return;
    float hp = 0, prev = 0, lp = 0;
    for (uint32_t i = 0; i < total; i++) {
        float n = noise();
        hp = 0.85f * (hp + n - prev);   // Hochpass -> hell, zischend
        prev = n;
        float v = 0;
        if (i < whoosh) {
            float t = (float)i / whoosh;
            v = hp * 0.35f * t * t;     // anschwellendes Zischen
        } else {
            float t = (float)(i - whoosh) / RATE;
            float crack = (t < 0.002f ? 1.0f : expf(-(t - 0.002f) / 0.012f));
            lp += 0.5f * (n - lp);
            v = (hp * 0.8f + lp * 0.6f) * crack * 1.6f;
            v += hp * 0.12f * expf(-t / 0.09f);   // Raumhall
        }
        s_whip[i] = clip(v);
    }
}

// Schuss: Knall (Rauschen, sofort voll), tiefer Wumms, langer Nachhall
void makeGun() {
    const uint32_t total = RATE * 0.75f;
    s_gunLen = total;
    s_gun = static_cast<int16_t*>(heap_caps_malloc(total * 2, MALLOC_CAP_SPIRAM));
    if (!s_gun) return;
    float lp = 0, lp2 = 0, phase = 0;
    for (uint32_t i = 0; i < total; i++) {
        float t = (float)i / RATE;
        float n = noise();
        lp += 0.35f * (n - lp);          // etwas dumpfer als die Peitsche
        lp2 += 0.08f * (n - lp2);        // für den Hall
        float blast = expf(-t / 0.035f);
        float freq = 180.0f * expf(-t / 0.05f) + 70.0f;
        phase += 2 * PI * freq / RATE;
        float thump = sinf(phase) * expf(-t / 0.07f);
        float tail = lp2 * 1.8f * expf(-t / 0.22f);
        float v = (t < 0.003f ? n * 1.2f : 0) + lp * 1.5f * blast + thump * 0.9f + tail * 0.5f;
        s_gun[i] = clip(v);
    }
}

bool installSpeaker() {
    i2s_config_t cfg = {};
    cfg.mode                 = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 3;
    cfg.dma_buf_len          = BLK;
    cfg.tx_desc_auto_clear   = true;   // nichts zu spielen -> Stille
    if (i2s_driver_install(SPK, &cfg, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_I2S_BCK;
    pins.ws_io_num    = PIN_I2S_WS;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(SPK, &pins) != ESP_OK) {
        i2s_driver_uninstall(SPK);
        return false;
    }
    i2s_zero_dma_buffer(SPK);
    return true;
}

void task(void*) {
    static int16_t out[BLK * 2];
    while (s_open) {
        if (s_pending < 0) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
            continue;
        }
        int which = s_pending;
        s_pending = -1;
        const int16_t* pcm = which == (int)Sound::Whip ? s_whip : s_gun;
        uint32_t len = which == (int)Sound::Whip ? s_whipLen : s_gunLen;
        if (!pcm) continue;
        for (uint32_t pos = 0; pos < len && s_open && s_pending < 0; pos += BLK) {
            size_t n = min<uint32_t>(BLK, len - pos);
            for (size_t i = 0; i < BLK; i++) out[2 * i] = out[2 * i + 1] = i < n ? pcm[pos + i] : 0;
            size_t w;
            i2s_write(SPK, out, sizeof(out), &w, pdMS_TO_TICKS(100));
        }
    }
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool open() {
    if (s_open) return true;
    if (!s_whip) makeWhip();
    if (!s_gun) makeGun();
    power::keepAwake(power::AWAKE_AUDIO, true);
    power::speaker(true);
    delay(5);
    if (!installSpeaker()) {
        power::speaker(false);
        power::keepAwake(power::AWAKE_AUDIO, false);
        return false;
    }
    s_open = true;
    s_pending = -1;
    if (xTaskCreatePinnedToCore(task, "sfx", 3072, nullptr, 5, &s_task, 0) != pdPASS) {
        s_open = false;
        i2s_driver_uninstall(SPK);
        power::speaker(false);
        power::keepAwake(power::AWAKE_AUDIO, false);
        return false;
    }
    return true;
}

void close() {
    if (!s_open) return;
    s_open = false;
    if (s_task) xTaskNotifyGive(s_task);
    for (int i = 0; i < 40 && s_task; i++) delay(10);
    i2s_driver_uninstall(SPK);
    power::speaker(false);
    power::keepAwake(power::AWAKE_AUDIO, false);
}

bool isOpen() { return s_open; }

void play(Sound s) {
    if (!s_open) return;
    s_pending = (int)s;
    if (s_task) xTaskNotifyGive(s_task);
}

}  // namespace sfx
