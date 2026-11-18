#include "sfx.h"

#include <LittleFS.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "config/pins.h"
#include "core/data_store.h"
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

// Eigene Sounds vom Handy (µ-law im Flash, beim Öffnen nach PCM dekodiert)
constexpr uint32_t MAX_CUSTOM = RATE * 3;
int16_t* s_custom[2] = {};
uint32_t s_customLen[2] = {};
bool s_customLoaded = false;
uint8_t* s_rxBuf = nullptr;
uint32_t s_rxSize = 0, s_rxPos = 0;
int s_rxSlot = -1, s_rxChunks = 0;

const char* fileFor(int slot) { return slot == (int)Sound::Gun ? "/sfx_gun.ul" : "/sfx_whip.ul"; }
int slotOf(const String& s) { return s.startsWith("gun") ? (int)Sound::Gun : (int)Sound::Whip; }

int16_t ulawDecode(uint8_t v) {
    uint8_t u = ~v;
    int t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (u & 0x80) ? (0x84 - t) : (t - 0x84);
}

void loadCustom(int slot) {
    if (s_custom[slot]) { heap_caps_free(s_custom[slot]); s_custom[slot] = nullptr; }
    s_customLen[slot] = 0;
    if (!LittleFS.exists(fileFor(slot))) return;
    File f = LittleFS.open(fileFor(slot), "r");
    if (!f) return;
    uint32_t n = min<uint32_t>(f.size(), MAX_CUSTOM);
    auto* raw = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM));
    auto* pcm = static_cast<int16_t*>(heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM));
    if (raw && pcm && f.read(raw, n) == n) {
        for (uint32_t i = 0; i < n; i++) pcm[i] = ulawDecode(raw[i]);
        s_custom[slot] = pcm;
        s_customLen[slot] = n;
        pcm = nullptr;
    }
    f.close();
    if (raw) heap_caps_free(raw);
    if (pcm) heap_caps_free(pcm);
}

bool handle(const String& topic, const String& payload) {
    if (topic == "SFX") {
        if (!s_rxBuf) return true;
        size_t out = 0;
        if (mbedtls_base64_decode(s_rxBuf + s_rxPos, s_rxSize - s_rxPos, &out,
                                  reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length()) != 0) {
            heap_caps_free(s_rxBuf);
            s_rxBuf = nullptr;
            store::sendToPhone("BEAT_ACK", "-1");
            return true;
        }
        s_rxPos += out;
        if (++s_rxChunks % 8 == 0) store::sendToPhone("BEAT_ACK", String(s_rxChunks));
        return true;
    }
    if (topic == "SFX_BEGIN") {   // SFX_BEGIN|whip|<bytes>
        int sep = payload.indexOf('|');
        s_rxSlot = slotOf(payload);
        s_rxSize = min<uint32_t>(payload.substring(sep + 1).toInt(), MAX_CUSTOM);
        s_rxPos = 0;
        s_rxChunks = 0;
        if (s_rxBuf) heap_caps_free(s_rxBuf);
        s_rxBuf = s_rxSize ? static_cast<uint8_t*>(heap_caps_malloc(s_rxSize, MALLOC_CAP_SPIRAM)) : nullptr;
        store::sendToPhone("BEAT_ACK", s_rxBuf ? "0" : "-1");
        return true;
    }
    if (topic == "SFX_END") {
        bool ok = s_rxBuf && s_rxPos >= s_rxSize;
        if (ok) {
            File f = LittleFS.open(fileFor(s_rxSlot), "w");
            ok = f && f.write(s_rxBuf, s_rxSize) == s_rxSize;
            if (f) f.close();
            if (ok) loadCustom(s_rxSlot);
        }
        if (s_rxBuf) heap_caps_free(s_rxBuf);
        s_rxBuf = nullptr;
        store::sendToPhone("BEAT_ACK", ok ? "done" : "-1");
        return true;
    }
    if (topic == "SFX_RESET") {
        int slot = slotOf(payload);
        LittleFS.remove(fileFor(slot));
        loadCustom(slot);
        return true;
    }
    return false;
}

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
        if (s_custom[which]) {   // eigener Sound vom Handy hat Vorrang
            pcm = s_custom[which];
            len = s_customLen[which];
        }
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

void begin() { store::addHandler(handle); }

bool open() {
    if (s_open) return true;
    if (!s_customLoaded) {
        loadCustom((int)Sound::Whip);
        loadCustom((int)Sound::Gun);
        s_customLoaded = true;
    }
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
