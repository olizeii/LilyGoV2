#include "sfx.h"

#include <LittleFS.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "config/pins.h"
#include "core/data_store.h"
#include "core/settings.h"
#include "hal/power.h"

namespace sfx {
namespace {

constexpr i2s_port_t SPK   = I2S_NUM_1;
constexpr uint32_t RATE    = 16000;
constexpr size_t BLK       = 256;

// Eigene Sounds vom Handy (µ-law im Flash, beim Öffnen nach PCM dekodiert)
constexpr uint32_t MAX_CUSTOM = RATE * 3;
constexpr int CUSTOM_SLOTS = 3;   // Whip, Gun, Burst
int16_t* s_custom[CUSTOM_SLOTS] = {};
uint32_t s_customLen[CUSTOM_SLOTS] = {};
uint8_t s_burstHz = 12, s_burstThr = 9;   // Dauerfeuer: Schuss/s, Schwelle in 0,1 g
bool s_customLoaded = false;
uint8_t* s_rxBuf = nullptr;
uint32_t s_rxSize = 0, s_rxPos = 0;
int s_rxSlot = -1, s_rxChunks = 0;

const char* fileFor(int slot) {
    return slot == (int)Sound::Gun ? "/sfx_gun.ul" : slot == (int)Sound::Burst ? "/sfx_burst.ul" : "/sfx_whip.ul";
}
int slotOf(const String& s) {
    return s.startsWith("gun") ? (int)Sound::Gun : s.startsWith("burst") ? (int)Sound::Burst : (int)Sound::Whip;
}

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
    if (topic == "GEST_CFG") {   // GEST_CFG|<schuss/s>|<schwelle*10>
        int sep = payload.indexOf('|');
        s_burstHz = constrain(payload.toInt(), 2, 25);
        if (sep > 0) s_burstThr = constrain(payload.substring(sep + 1).toInt(), 2, 30);
        settings::putU8("burstHz", s_burstHz);
        settings::putU8("burstThr", s_burstThr);
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
volatile int s_pending = -1;     // Sound, der als Nächstes kommt
volatile bool s_hum = false;     // Lichtschwert-Summen an
volatile float s_motion = 0;     // 0..~1.2 – Bewegung moduliert das Summen

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

int16_t* alloc(uint32_t n) { return static_cast<int16_t*>(heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM)); }

// Lichtschwert-Grundton: Obertöne + leicht verstimmte Quinte
float saberWave(float& p1, float& p2, float f) {
    p1 += 2 * PI * f / RATE;
    if (p1 > 2 * PI) p1 -= 2 * PI;
    p2 += 2 * PI * f * 1.503f / RATE;
    if (p2 > 2 * PI) p2 -= 2 * PI;
    return 0.5f * sinf(p1) + 0.25f * sinf(2 * p1) + 0.14f * sinf(3 * p1) + 0.08f * sinf(5 * p1) + 0.22f * sinf(p2);
}

struct Clip { int16_t* pcm = nullptr; uint32_t len = 0; };
Clip s_clips[(int)Sound::COUNT];

// Peitsche: kurzes Zischen (Seil), dann ein sehr harter, heller Knall mit kurzem Nachhall
void makeWhip(Clip& c) {
    const uint32_t whoosh = RATE * 0.09f, total = RATE * 0.42f;
    if (!(c.pcm = alloc(total))) return;
    c.len = total;
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
        c.pcm[i] = clip(v);
    }
}

// Schuss: Knall (Rauschen, sofort voll), tiefer Wumms, Nachhall.
// burst=true: knapper Schuss fürs Dauerfeuer (sonst verschwimmt die Salve)
void makeGun(Clip& c, bool burst) {
    const float T = burst ? 0.14f : 0.75f;
    const uint32_t total = RATE * T;
    if (!(c.pcm = alloc(total))) return;
    c.len = total;
    float lp = 0, lp2 = 0, phase = 0;
    for (uint32_t i = 0; i < total; i++) {
        float t = (float)i / RATE;
        float n = noise();
        lp += 0.35f * (n - lp);
        lp2 += 0.08f * (n - lp2);
        float blast = expf(-t / (burst ? 0.018f : 0.035f));
        float freq = 180.0f * expf(-t / 0.05f) + 70.0f;
        phase += 2 * PI * freq / RATE;
        float thump = sinf(phase) * expf(-t / (burst ? 0.03f : 0.07f));
        float tail = lp2 * 1.8f * expf(-t / (burst ? 0.04f : 0.22f));
        float v = (t < 0.003f ? n * 1.2f : 0) + lp * 1.5f * blast + thump * 0.9f + tail * 0.5f;
        if (burst) v *= min(1.0f, (T - t) / 0.02f);   // sauber ausblenden
        c.pcm[i] = clip(v);
    }
}

// Einschalten: Ton fährt von tief nach oben, Knistern, wird lauter
// Ausschalten: umgekehrt, Ton sackt ab und verstummt
void makeSaberSweep(Clip& c, bool on) {
    const float T = on ? 0.75f : 0.5f;
    const uint32_t total = RATE * T;
    if (!(c.pcm = alloc(total))) return;
    c.len = total;
    float p1 = 0, p2 = 0, hp = 0, prev = 0;
    for (uint32_t i = 0; i < total; i++) {
        float t = (float)i / RATE, x = t / T;   // 0..1
        float k = on ? x : 1 - x;               // wie weit die Klinge draußen ist
        float f = 30.0f + 70.0f * sqrtf(k) + (on ? 25.0f * expf(-x * 6) * sinf(x * 60) : 0);
        float n = noise();
        hp = 0.9f * (hp + n - prev);
        prev = n;
        float crackle = hp * (on ? 0.6f * expf(-x * 4) : 0.25f * (1 - x));
        float amp = on ? (0.35f + 0.65f * min(1.0f, x * 3)) : (1 - x) * (1 - x);
        float v = saberWave(p1, p2, f) * amp * 1.1f + crackle;
        if (on && t < 0.03f) v += n * (1 - t / 0.03f);   // "Tsch" beim Zünden
        c.pcm[i] = clip(v);
    }
}

// Schwung: Ton biegt nach oben und zurück (wie vorbeifliegend), dazu Luft-Rauschen
void makeSaberSwing(Clip& c) {
    const float T = 0.42f;
    const uint32_t total = RATE * T;
    if (!(c.pcm = alloc(total))) return;
    c.len = total;
    float p1 = 0, p2 = 0, bp = 0, lp = 0;
    for (uint32_t i = 0; i < total; i++) {
        float x = (float)i / total;
        float env = sinf(PI * x);
        env *= env;
        float f = 100.0f * (1 + 0.9f * env) - 15.0f * x;
        float n = noise();
        lp += 0.25f * (n - lp);
        bp += 0.5f * (lp - bp);
        float v = saberWave(p1, p2, f) * env * 1.2f + (lp - bp) * env * 1.4f;
        c.pcm[i] = clip(v);
    }
}

// Aufprall: heller Funken-Knall + metallisches Klingeln (unharmonische Teiltöne)
void makeSaberClash(Clip& c) {
    const float T = 0.5f;
    const uint32_t total = RATE * T;
    if (!(c.pcm = alloc(total))) return;
    c.len = total;
    float p1 = 0, p2 = 0, hp = 0, prev = 0;
    const float rings[] = {1180.0f, 1745.0f, 2630.0f, 3310.0f};
    for (uint32_t i = 0; i < total; i++) {
        float t = (float)i / RATE;
        float n = noise();
        hp = 0.8f * (hp + n - prev);
        prev = n;
        float ring = 0;
        for (int k = 0; k < 4; k++) ring += sinf(2 * PI * rings[k] * t) * expf(-t / (0.09f - 0.015f * k));
        float sparks = hp * expf(-t / 0.03f) * 1.6f + (noise() > 0.6f ? hp * 0.3f * expf(-t / 0.2f) : 0);
        float v = sparks + ring * 0.35f + saberWave(p1, p2, 140.0f - 60 * t) * 0.6f * expf(-t / 0.25f);
        c.pcm[i] = clip(v);
    }
}

void makeAll() {
    Clip* c = s_clips;
    if (!c[(int)Sound::Whip].pcm) makeWhip(c[(int)Sound::Whip]);
    if (!c[(int)Sound::Gun].pcm) makeGun(c[(int)Sound::Gun], false);
    if (!c[(int)Sound::Burst].pcm) makeGun(c[(int)Sound::Burst], true);
    if (!c[(int)Sound::SaberOn].pcm) makeSaberSweep(c[(int)Sound::SaberOn], true);
    if (!c[(int)Sound::SaberOff].pcm) makeSaberSweep(c[(int)Sound::SaberOff], false);
    if (!c[(int)Sound::SaberSwing].pcm) makeSaberSwing(c[(int)Sound::SaberSwing]);
    if (!c[(int)Sound::SaberClash].pcm) makeSaberClash(c[(int)Sound::SaberClash]);
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
    const int16_t* pcm = nullptr;
    uint32_t len = 0, pos = 0;
    int cur = -1;
    float humLvl = 0, p1 = 0, p2 = 0, motion = 0;
    while (s_open) {
        if (s_pending >= 0) {   // neuer Sound ersetzt den laufenden
            cur = s_pending;
            s_pending = -1;
            pcm = s_clips[cur].pcm;
            len = s_clips[cur].len;
            if (cur < CUSTOM_SLOTS && s_custom[cur]) {   // eigener Sound vom Handy hat Vorrang
                pcm = s_custom[cur];
                len = s_customLen[cur];
            }
            pos = 0;
        }
        bool clipOn = pcm && pos < len;
        if (!clipOn && !s_hum && humLvl < 0.002f) {
            humLvl = 0;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
            continue;
        }
        motion += 0.5f * (s_motion - motion);
        // Summen: beim Zünden erst gegen Ende einblenden; Bewegung -> lauter und höher
        bool igniting = clipOn && cur == (int)Sound::SaberOn && pos < len * 7 / 10;
        float target = (s_hum && !igniting) ? 0.3f + 0.45f * motion : 0;
        float f = 92.0f * (1 + 0.3f * motion);
        for (size_t i = 0; i < BLK; i++) {
            humLvl += 0.003f * (target - humLvl);
            float v = humLvl > 0.002f ? humLvl * saberWave(p1, p2, f) * (0.92f + 0.08f * noise()) : 0;
            if (pcm && pos < len) v += pcm[pos++] / 32768.0f;
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            out[2 * i] = out[2 * i + 1] = (int16_t)(v * 32000);
        }
        size_t w;
        i2s_write(SPK, out, sizeof(out), &w, pdMS_TO_TICKS(100));
    }
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

void begin() {
    s_burstHz = settings::getU8("burstHz", 12);
    s_burstThr = settings::getU8("burstThr", 9);
    store::addHandler(handle);
}

uint8_t burstHz() { return s_burstHz; }
float burstThreshold() { return s_burstThr / 10.0f; }

void setHum(bool on) {
    s_hum = on;
    if (s_task) xTaskNotifyGive(s_task);
}

void setMotion(float m) { s_motion = m; }

bool open() {
    if (s_open) return true;
    if (!s_customLoaded) {
        for (int i = 0; i < CUSTOM_SLOTS; i++) loadCustom(i);
        s_customLoaded = true;
    }
    makeAll();
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
    s_hum = false;
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
