#include "data_store.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

#include "ble_manager.h"
#include "hal/rtc_clock.h"

namespace store {

static WatchData s_data;
static std::vector<Listener> s_listeners[static_cast<size_t>(Topic::Count)];

static constexpr const char* NOTES_FILE = "/notes.dat";
static bool s_fsOk = false;

// Notizen werden zwischen NOTES_BEGIN und NOTES_END gesammelt
static std::vector<Note> s_pendingNotes;
static bool s_notesReceiving = false;

// Kartenbild: wird in einen Staging-Puffer dekodiert und bei MAP_END getauscht
static uint8_t* s_mapStage     = nullptr;
static uint32_t s_mapStageSize = 0;
static uint32_t s_mapStagePos  = 0;
static uint16_t s_mapW = 0, s_mapH = 0;

// --- Hilfsfunktionen ---------------------------------------------------------

WatchData& data() { return s_data; }

void subscribe(Topic topic, Listener listener) {
    s_listeners[static_cast<size_t>(topic)].push_back(std::move(listener));
}

void publish(Topic topic) {
    for (auto& l : s_listeners[static_cast<size_t>(topic)]) l();
}

bool sendToPhone(const String& cmd, const String& arg) { return BleManager::instance().send(cmd, arg); }

// "a|b|c" -> {a, b, c}. Der letzte Teil bekommt den Rest (darf das Trennzeichen enthalten).
static std::vector<String> split(const String& s, char sep, size_t maxParts) {
    std::vector<String> parts;
    int start = 0;
    while (parts.size() + 1 < maxParts) {
        int idx = s.indexOf(sep, start);
        if (idx < 0) break;
        parts.push_back(s.substring(start, idx));
        start = idx + 1;
    }
    parts.push_back(s.substring(start));
    return parts;
}

static String part(const std::vector<String>& p, size_t i) { return i < p.size() ? p[i] : String(); }

static int clampPercent(const String& s) {
    int v = s.toInt();
    return v < 0 ? -1 : constrain(v, 0, 100);
}

// --- Notizen (LittleFS) ------------------------------------------------------
// Datei: pro Notiz  id \x1F titel \x1F text \x1E

static void loadNotes() {
    s_data.notes.clear();
    if (!s_fsOk || !LittleFS.exists(NOTES_FILE)) return;
    File f = LittleFS.open(NOTES_FILE, "r");
    if (!f) return;
    String all = f.readString();
    f.close();
    for (auto& rec : split(all, '\x1E', 200)) {
        if (!rec.length()) continue;
        auto p = split(rec, '\x1F', 3);
        s_data.notes.push_back({part(p, 0), part(p, 1), part(p, 2)});
    }
}

static void saveNotes() {
    if (!s_fsOk) return;
    File f = LittleFS.open(NOTES_FILE, "w");
    if (!f) return;
    for (auto& n : s_data.notes) {
        f.print(n.id);
        f.write('\x1F');
        f.print(n.title);
        f.write('\x1F');
        f.print(n.body);
        f.write('\x1E');
    }
    f.close();
}

void begin() {
    // Default-Partition "spiffs" (default_16MB.csv) wird als LittleFS genutzt
    s_fsOk = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    if (!s_fsOk) log_e("LittleFS nicht verfügbar");
    loadNotes();
}

// --- Benachrichtigungen ------------------------------------------------------

static void addMessage(PhoneMessage m, bool alert = true) {
    // gleiche Notification aktualisiert (z.B. WhatsApp-Verlauf) -> alte ersetzen
    for (auto it = s_data.messages.begin(); it != s_data.messages.end(); ++it) {
        if (it->key == m.key) {
            s_data.messages.erase(it);
            break;
        }
    }
    m.receivedAt = time(nullptr);
    s_data.messages.push_front(m);
    while (s_data.messages.size() > MAX_MESSAGES) s_data.messages.pop_back();
    publish(Topic::Messages);
    if (alert) {
        s_data.alert = m;
        publish(Topic::Alert);
    }
}

static void removeMessage(const String& key) {
    for (auto it = s_data.messages.begin(); it != s_data.messages.end(); ++it) {
        if (it->key == key) {
            s_data.messages.erase(it);
            publish(Topic::Messages);
            return;
        }
    }
}

void dismissMessage(const String& key) {
    removeMessage(key);
    sendToPhone("NOTIF_DISMISS", key);
}

// --- Kartenbild ----------------------------------------------------------------

static void mapBegin(const std::vector<String>& p) {
    if (s_mapStage) heap_caps_free(s_mapStage);
    s_mapW = part(p, 0).toInt();
    s_mapH = part(p, 1).toInt();
    s_mapStageSize = part(p, 2).toInt();
    s_mapStagePos = 0;
    // Plausibilität: 16 Farben * 4 Byte + w*h/2
    uint32_t expect = 64 + uint32_t(s_mapW) * s_mapH / 2;
    if (s_mapW == 0 || s_mapW > 240 || s_mapH == 0 || s_mapH > 240 || s_mapStageSize != expect) {
        log_w("MAP_BEGIN ungültig");
        s_mapStage = nullptr;
        return;
    }
    s_mapStage = static_cast<uint8_t*>(heap_caps_malloc(s_mapStageSize, MALLOC_CAP_SPIRAM));
}

static void mapChunk(const String& b64) {
    if (!s_mapStage) return;
    size_t out = 0;
    int rc = mbedtls_base64_decode(s_mapStage + s_mapStagePos, s_mapStageSize - s_mapStagePos, &out,
                                   reinterpret_cast<const unsigned char*>(b64.c_str()), b64.length());
    if (rc != 0) {
        log_w("MAP base64 Fehler %d", rc);
        heap_caps_free(s_mapStage);
        s_mapStage = nullptr;
        return;
    }
    s_mapStagePos += out;
}

static void mapEnd() {
    if (!s_mapStage || s_mapStagePos != s_mapStageSize) {
        if (s_mapStage) heap_caps_free(s_mapStage);
        s_mapStage = nullptr;
        return;
    }
    uint8_t* old = s_data.map.data;
    s_data.map.data = s_mapStage;
    s_data.map.size = s_mapStageSize;
    s_data.map.w = s_mapW;
    s_data.map.h = s_mapH;
    s_data.map.version++;
    s_mapStage = nullptr;
    publish(Topic::Map);  // Listener zeigen ab jetzt den neuen Puffer
    if (old) heap_caps_free(old);
}

// --- Parser --------------------------------------------------------------------

static std::vector<TopicHandler> s_handlers;

void addHandler(TopicHandler handler) { s_handlers.push_back(std::move(handler)); }

void handleMessage(const String& msg) {
    int sep = msg.indexOf('|');
    String topic   = (sep < 0 ? msg : msg.substring(0, sep));
    String payload = (sep < 0 ? "" : msg.substring(sep + 1));
    topic.trim();
    topic.toUpperCase();

    if (topic == "MAP") {  // häufigstes Topic bei Kartenübertragung zuerst
        mapChunk(payload);

    } else if (topic == "TIME") {
        // TIME|<unix epoch UTC>
        rtc_clock::setEpoch(static_cast<time_t>(strtoll(payload.c_str(), nullptr, 10)));
        publish(Topic::Time);

    } else if (topic == "BAT") {
        // BAT|phone|82|1   BAT|rayban|64   (-1 = nicht verbunden)
        auto p = split(payload, '|', 3);
        String dev = part(p, 0);
        dev.toLowerCase();
        if (dev == "phone") {
            s_data.phoneBattery  = clampPercent(part(p, 1));
            s_data.phoneCharging = part(p, 2) == "1";
        } else if (dev == "rayban") {
            s_data.raybanBattery = clampPercent(part(p, 1));
        }
        publish(Topic::Battery);

    } else if (topic == "MEDIA" || topic == "SPOTIFY") {
        // MEDIA|<titel>|<artist>|<1=spielt>|<pos s>|<dauer s>|<app>
        auto p = split(payload, '|', 6);
        MediaState& m = s_data.media;
        m.active  = true;
        m.title   = part(p, 0);
        m.artist  = part(p, 1);
        m.playing = part(p, 2).toInt() == 1;
        m.posSec  = part(p, 3).toInt();
        m.durSec  = part(p, 4).toInt();
        m.app     = p.size() > 5 ? p[5] : String("Spotify");
        m.rxMs    = millis();
        publish(Topic::Media);

    } else if (topic == "MEDIA_OFF") {
        s_data.media = MediaState();
        publish(Topic::Media);

    } else if (topic == "UNTIS") {
        // UNTIS|#Titel\n08:00;08:45;Mathe;R204;MUE;N;Info\n...
        s_data.untis.clear();
        s_data.untisTitle = "";
        for (auto& line : split(payload, '\n', 40)) {
            line.trim();
            if (!line.length()) continue;
            if (line[0] == '#') {
                s_data.untisTitle = line.substring(1);
                continue;
            }
            auto f = split(line, ';', 7);
            Lesson l;
            l.start   = part(f, 0);
            l.end     = part(f, 1);
            l.subject = part(f, 2);
            l.room    = part(f, 3);
            l.teacher = part(f, 4);
            l.status  = part(f, 5).length() ? part(f, 5)[0] : 'N';
            l.info    = part(f, 6);
            s_data.untis.push_back(l);
        }
        publish(Topic::Untis);

    } else if (topic == "NOTIF" || topic == "NOTIF_S") {
        // NOTIF|<key>|<app>|<titel>|<text>   (NOTIF_S = still nachgeliefert, kein Banner)
        auto p = split(payload, '|', 4);
        addMessage({part(p, 0), part(p, 1), part(p, 2), part(p, 3), 0}, topic == "NOTIF");

    } else if (topic == "MSG") {
        // Altformat (Tasker): MSG|<app>|<titel>|<text>
        auto p = split(payload, '|', 3);
        addMessage({"t" + String(millis()), part(p, 0), part(p, 1), part(p, 2), 0});

    } else if (topic == "NOTIF_DEL") {
        removeMessage(payload);

    } else if (topic == "MSG_CLEAR") {
        s_data.messages.clear();
        publish(Topic::Messages);

    } else if (topic == "NOTES_BEGIN") {
        s_pendingNotes.clear();
        s_notesReceiving = true;

    } else if (topic == "NOTE") {
        // NOTE|<id>|<titel>|<text mit \n>
        if (!s_notesReceiving) return;
        auto p = split(payload, '|', 3);
        s_pendingNotes.push_back({part(p, 0), part(p, 1), part(p, 2)});

    } else if (topic == "NOTES_END") {
        if (!s_notesReceiving) return;
        s_notesReceiving = false;
        s_data.notes.swap(s_pendingNotes);
        s_pendingNotes.clear();
        saveNotes();
        publish(Topic::Notes);

    } else if (topic == "NOTES") {
        // Altformat: eine einzige Notiz
        s_data.notes = {{"tasker", "Notiz", payload}};
        saveNotes();
        publish(Topic::Notes);

    } else if (topic == "GPS") {
        // GPS|48.1371,11.5754|Marienplatz, München
        auto p = split(payload, '|', 2);
        s_data.location = part(p, 0);
        s_data.location.replace(",", ", ");
        s_data.place = part(p, 1);
        publish(Topic::Location);

    } else if (topic == "NAV") {
        // NAV|<manöver>|<distanz>|<anweisung>|<ankunft>
        auto p = split(payload, '|', 4);
        NavState& n = s_data.nav;
        n.active      = true;
        n.maneuver    = part(p, 0);
        n.distance    = part(p, 1);
        n.instruction = part(p, 2);
        n.eta         = part(p, 3);
        publish(Topic::Nav);

    } else if (topic == "NAV_END") {
        s_data.nav = NavState();
        publish(Topic::Nav);

    } else if (topic == "MAP_BEGIN") {
        // MAP_BEGIN|<w>|<h>|<bytes>
        mapBegin(split(payload, '|', 3));

    } else if (topic == "MAP_END") {
        mapEnd();

    } else if (topic == "SHORTCUT") {
        // SHORTCUT|1|Licht an
        auto p = split(payload, '|', 2);
        int idx = p[0].toInt() - 1;
        if (idx < 0 || idx > 1 || p.size() < 2) return;
        s_data.shortcutLabel[idx] = p[1];
        publish(Topic::Shortcuts);

    } else {
        for (auto& h : s_handlers)
            if (h(topic, payload)) return;
        log_w("Unbekanntes Topic: %s", topic.c_str());
    }
}

}  // namespace store
