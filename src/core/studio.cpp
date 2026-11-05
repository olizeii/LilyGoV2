#include "studio.h"

#include <LittleFS.h>
#include <driver/i2s.h>
#include <lvgl.h>
#include <mbedtls/base64.h>

#include "ble_manager.h"
#include "config/pins.h"
#include "core/data_store.h"
#include "core/voice.h"
#include "hal/power.h"

namespace studio {
namespace {

constexpr i2s_port_t SPK      = I2S_NUM_1;   // I2S0 gehört dem PDM-Mikro
constexpr size_t BLK          = 256;         // 16 ms
constexpr uint32_t BEAT_MAX   = MAX_BEAT_S * RATE;
constexpr uint32_t CLIP_MAX   = MAX_CLIP_S * RATE;
constexpr uint32_t LATENCY    = RATE / 10;   // ~100 ms Ein-/Ausgabe-Verzögerung beim Aufnehmen ausgleichen
constexpr int WINDOW          = 8;           // Pakete pro BEAT_ACK

const char* F_BEAT = "/st_beat.ul";
const char* F_META = "/st_meta.txt";

uint8_t* s_beat = nullptr;
uint32_t s_beatLen = 0;
String s_beatName;
uint8_t* s_clipBuf[MAX_CLIPS] = {};
Clip s_clips[MAX_CLIPS];
uint8_t s_beatVol = 80;

volatile bool s_run = false;
volatile bool s_rec = false;
volatile uint32_t s_pos = 0;
volatile uint32_t s_endPos = 0;
volatile float s_level = 0;
volatile int s_recIdx = -1;
volatile int s_saveClip = -1;   // vom Audio-Task gesetzt, im LVGL-Timer gespeichert
TaskHandle_t s_task = nullptr;
uint32_t s_rev = 1;

// Übertragung
bool s_rx = false;
uint32_t s_rxSize = 0, s_rxPos = 0;
int s_rxChunks = 0;
String s_rxName;

// --- µ-law (G.711) -----------------------------------------------------------
int16_t s_ulaw[256];

void buildTable() {
    for (int i = 0; i < 256; i++) {
        uint8_t u = ~i;
        int t = ((u & 0x0F) << 3) + 0x84;
        t <<= (u & 0x70) >> 4;
        s_ulaw[i] = (u & 0x80) ? (0x84 - t) : (t - 0x84);
    }
}

uint8_t encode(int16_t pcm) {
    int sign = (pcm >> 8) & 0x80;
    int v = sign ? -(int)pcm : pcm;
    if (v > 32635) v = 32635;
    v += 0x84;
    int exp = 7;
    for (int mask = 0x4000; (v & mask) == 0 && exp > 0; mask >>= 1) exp--;
    int mant = (v >> (exp + 3)) & 0x0F;
    return ~(sign | (exp << 4) | mant);
}

// --- Speicher ----------------------------------------------------------------
String clipFile(int i) { return String("/st_clip") + i + ".ul"; }

void saveMeta() {
    File f = LittleFS.open(F_META, "w");
    if (!f) return;
    f.println(s_beatName);
    f.println(s_beatVol);
    for (auto& c : s_clips) f.printf("%d %lu %lu\n", c.used ? 1 : 0, (unsigned long)c.offset, (unsigned long)c.length);
    f.close();
}

void saveBuf(const String& path, const uint8_t* buf, uint32_t len) {
    File f = LittleFS.open(path, "w");
    if (!f) return;
    for (uint32_t i = 0; i < len; i += 4096) f.write(buf + i, min<uint32_t>(4096, len - i));
    f.close();
}

uint32_t loadBuf(const String& path, uint8_t* buf, uint32_t max) {
    if (!LittleFS.exists(path)) return 0;
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t n = f.read(buf, max);
    f.close();
    return n;
}

void load() {
    s_beatLen = loadBuf(F_BEAT, s_beat, BEAT_MAX);
    if (!LittleFS.exists(F_META)) return;
    File f = LittleFS.open(F_META, "r");
    if (!f) return;
    s_beatName = f.readStringUntil('\n');
    s_beatName.trim();
    s_beatVol = constrain(f.readStringUntil('\n').toInt(), 0, 100);
    for (int i = 0; i < MAX_CLIPS; i++) {
        String line = f.readStringUntil('\n');
        int used = 0;
        unsigned long off = 0, len = 0;
        if (sscanf(line.c_str(), "%d %lu %lu", &used, &off, &len) != 3 || !used) continue;
        uint32_t got = loadBuf(clipFile(i), s_clipBuf[i], CLIP_MAX);
        if (got == 0) continue;
        s_clips[i] = {true, (uint32_t)off, min<uint32_t>(got, len)};
    }
    f.close();
    if (!s_beatLen) s_beatName = "";
}

// --- Audio -------------------------------------------------------------------
bool installSpeaker() {
    i2s_config_t cfg = {};
    cfg.mode                 = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = BLK;
    cfg.tx_desc_auto_clear   = true;
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
    static int16_t mic[BLK];
    float prevX = 0, prevY = 0;
    uint32_t recLen = 0;
    const int rec = s_recIdx;

    while (s_run) {
        uint32_t pos = s_pos;
        if (!s_rec && pos >= s_endPos) break;

        // Aufnahme: Mikro-Block holen, Gleichanteil weg, verstärken, als µ-law ablegen
        if (s_rec) {
            size_t n = voice::micRead(mic, BLK, 100);
            uint8_t* dst = s_clipBuf[rec];
            int peak = 0;
            for (size_t i = 0; i < n && recLen < CLIP_MAX; i++) {
                float x = mic[i];
                float y = x - prevX + 0.995f * prevY;
                prevX = x;
                prevY = y;
                int v = (int)(y * 5.0f);
                v = constrain(v, -32000, 32000);
                if (abs(v) > peak) peak = abs(v);
                dst[recLen++] = encode((int16_t)v);
            }
            s_level = s_level * 0.6f + 0.4f * min(1.0f, peak / 20000.0f);
            if (recLen >= CLIP_MAX || pos >= s_endPos) s_rec = false;
        }

        // Mischen: Beat + alle fertigen Spuren
        const float beatGain = s_beatVol / 100.0f * 0.8f;
        int outPeak = 0;
        for (size_t i = 0; i < BLK; i++) {
            uint32_t p = pos + i;
            float v = 0;
            if (p < s_beatLen) v += s_ulaw[s_beat[p]] * beatGain;
            for (int c = 0; c < MAX_CLIPS; c++) {
                const Clip& cl = s_clips[c];
                if (!cl.used || p < cl.offset || p >= cl.offset + cl.length) continue;
                v += s_ulaw[s_clipBuf[c][p - cl.offset]] * 0.9f;
            }
            int s = constrain((int)v, -32767, 32767);
            if (abs(s) > outPeak) outPeak = abs(s);
            out[2 * i] = out[2 * i + 1] = (int16_t)s;
        }
        if (!s_rec && rec < 0) s_level = s_level * 0.6f + 0.4f * min(1.0f, outPeak / 20000.0f);
        size_t written = 0;
        i2s_write(SPK, out, sizeof(out), &written, pdMS_TO_TICKS(200));
        s_pos = pos + BLK;
        if (rec >= 0 && !s_rec) break;   // Aufnahme beendet -> auch Wiedergabe stoppen
    }

    // Aufnahme abschließen: Verzögerung ausgleichen (Anfang wegschneiden)
    if (rec >= 0) {
        voice::micClose();
        if (recLen > LATENCY + RATE / 4) {
            memmove(s_clipBuf[rec], s_clipBuf[rec] + LATENCY, recLen - LATENCY);
            s_clips[rec].length = recLen - LATENCY;
            s_clips[rec].used   = true;
            s_saveClip = rec;
        }
    }

    // Ausklingen lassen, dann alles aus
    memset(out, 0, sizeof(out));
    size_t w;
    for (int i = 0; i < 4; i++) i2s_write(SPK, out, sizeof(out), &w, pdMS_TO_TICKS(50));
    i2s_driver_uninstall(SPK);
    power::speaker(false);

    s_rec = false;
    s_recIdx = -1;
    s_level = 0;
    s_run = false;
    s_rev++;
    s_task = nullptr;
    vTaskDelete(nullptr);
}

bool startTask() {
    power::speaker(true);
    delay(5);
    if (!installSpeaker()) {
        power::speaker(false);
        return false;
    }
    s_run = true;
    if (xTaskCreatePinnedToCore(task, "studio", 4096, nullptr, 4, &s_task, 0) != pdPASS) {
        s_run = false;
        i2s_driver_uninstall(SPK);
        power::speaker(false);
        return false;
    }
    s_rev++;
    return true;
}

// --- Beat vom Handy ----------------------------------------------------------
bool handle(const String& topic, const String& payload) {
    if (topic == "BEAT") {
        if (!s_rx) return true;
        size_t out = 0;
        int rc = mbedtls_base64_decode(s_beat + s_rxPos, s_rxSize - s_rxPos, &out,
                                       reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
        if (rc != 0) {
            log_w("BEAT base64 Fehler %d", rc);
            s_rx = false;
            store::sendToPhone("BEAT_ACK", "-1");
            BleManager::instance().setFastLink(false);
            s_rev++;
            return true;
        }
        s_rxPos += out;
        if (++s_rxChunks % WINDOW == 0) store::sendToPhone("BEAT_ACK", String(s_rxChunks));
        return true;
    }
    if (topic == "BEAT_BEGIN") {
        // BEAT_BEGIN|<name>|<bytes>
        stop();
        int sep = payload.lastIndexOf('|');
        s_rxName = payload.substring(0, sep);
        s_rxSize = min<uint32_t>(payload.substring(sep + 1).toInt(), BEAT_MAX);
        s_rxPos = 0;
        s_rxChunks = 0;
        s_rx = s_rxSize > 0;
        s_beatLen = 0;   // alter Beat ist ab jetzt überschrieben
        BleManager::instance().setFastLink(true);
        store::sendToPhone("BEAT_ACK", "0");
        s_rev++;
        return true;
    }
    if (topic == "BEAT_END") {
        BleManager::instance().setFastLink(false);
        if (s_rx && s_rxPos >= s_rxSize) {
            s_beatLen = s_rxSize;
            s_beatName = s_rxName;
            saveBuf(F_BEAT, s_beat, s_beatLen);
            saveMeta();
            store::sendToPhone("BEAT_ACK", "done");
        } else {
            store::sendToPhone("BEAT_ACK", "-1");
        }
        s_rx = false;
        s_rev++;
        return true;
    }
    return false;
}

}  // namespace

void begin() {
    buildTable();
    s_beat = static_cast<uint8_t*>(heap_caps_malloc(BEAT_MAX, MALLOC_CAP_SPIRAM));
    for (auto& b : s_clipBuf) b = static_cast<uint8_t*>(heap_caps_malloc(CLIP_MAX, MALLOC_CAP_SPIRAM));
    if (!s_beat || !s_clipBuf[MAX_CLIPS - 1]) {
        log_e("Studio: kein PSRAM");
        return;
    }
    load();
    store::addHandler(handle);

    // Fertige Aufnahmen im UI-Thread sichern (Flash-Schreiben nicht im Audio-Task)
    lv_timer_create([](lv_timer_t*) {
        int c = s_saveClip;
        if (c < 0 || s_run) return;
        s_saveClip = -1;
        saveBuf(clipFile(c), s_clipBuf[c], s_clips[c].length);
        saveMeta();
        s_rev++;
    }, 250, nullptr);
}

bool hasBeat() { return s_beatLen > 0; }
const String& beatName() { return s_beatName; }
uint32_t beatLength() { return s_beatLen; }
const Clip& clip(uint8_t i) { return s_clips[i < MAX_CLIPS ? i : 0]; }

uint32_t songLength() {
    uint32_t len = s_beatLen;
    for (auto& c : s_clips)
        if (c.used) len = max(len, c.offset + c.length);
    return len;
}

uint32_t position() { return s_pos; }
bool isPlaying() { return s_run; }
bool isRecording() { return s_run && s_rec; }
int recordingClip() { return s_run ? s_recIdx : -1; }
float level() { return s_level; }
int transferPercent() { return s_rx && s_rxSize ? (int)(100ull * s_rxPos / s_rxSize) : -1; }
uint32_t revision() { return s_rev; }

void play(uint32_t from) {
    if (s_run || s_rx || !s_beat) return;
    uint32_t len = songLength();
    if (!len) return;
    if (from >= len) from = 0;
    s_pos = from;
    s_endPos = len;
    s_recIdx = -1;
    s_rec = false;
    startTask();
}

bool record(uint32_t from) {
    if (s_run || s_rx || !s_beat) return false;
    int idx = -1;
    for (int i = 0; i < MAX_CLIPS; i++)
        if (!s_clips[i].used) { idx = i; break; }
    if (idx < 0) return false;
    if (!voice::micOpen()) return false;

    // Mit Beat: bis zum Beat-Ende (oder max. 20 s), ohne Beat: 20 s
    if (s_beatLen && from >= s_beatLen) from = 0;
    s_pos = from;
    s_endPos = s_beatLen ? min(s_beatLen, from + CLIP_MAX) : from + CLIP_MAX;
    s_clips[idx] = {false, from, 0};
    s_recIdx = idx;
    s_rec = true;
    if (!startTask()) {
        voice::micClose();
        s_rec = false;
        s_recIdx = -1;
        return false;
    }
    return true;
}

void stop() {
    if (!s_run) return;
    s_rec = false;
    s_run = false;
    for (int i = 0; i < 40 && s_task; i++) delay(10);   // Task räumt selbst auf
}

void moveClip(uint8_t i, uint32_t newOffset) {
    if (i >= MAX_CLIPS || s_run || !s_clips[i].used) return;
    s_clips[i].offset = newOffset;
    saveMeta();
    s_rev++;
}

void deleteClip(uint8_t i) {
    if (i >= MAX_CLIPS || s_run) return;
    s_clips[i] = Clip();
    LittleFS.remove(clipFile(i));
    saveMeta();
    s_rev++;
}

void setBeatVolume(uint8_t pct) {
    s_beatVol = min<uint8_t>(pct, 100);
    saveMeta();
    s_rev++;
}

uint8_t beatVolume() { return s_beatVol; }

}  // namespace studio
