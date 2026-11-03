#include "data_store.h"

#include "core/settings.h"
#include "hal/rtc_clock.h"

namespace store {

static WatchData s_data;
static std::vector<Listener> s_listeners[static_cast<size_t>(Topic::Count)];

static constexpr const char* KEY_NOTES = "notes";
static constexpr size_t NVS_STRING_MAX = 3900;  // NVS-Strings max. ~4000 Byte

void begin() { s_data.notes = settings::getString(KEY_NOTES); }

WatchData& data() { return s_data; }

void subscribe(Topic topic, Listener listener) {
    s_listeners[static_cast<size_t>(topic)].push_back(std::move(listener));
}

void publish(Topic topic) {
    for (auto& l : s_listeners[static_cast<size_t>(topic)]) l();
}

// "a|b|c" -> {a, b, c}. Der letzte Teil bekommt den Rest (darf '|' enthalten).
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

static int clampPercent(const String& s) { return constrain(s.toInt(), 0, 100); }

void handleMessage(const String& msg) {
    int sep = msg.indexOf('|');
    String topic   = (sep < 0 ? msg : msg.substring(0, sep));
    String payload = (sep < 0 ? "" : msg.substring(sep + 1));
    topic.trim();
    topic.toUpperCase();

    if (topic == "TIME") {
        // TIME|<unix epoch UTC>
        rtc_clock::setEpoch(static_cast<time_t>(strtoll(payload.c_str(), nullptr, 10)));
        publish(Topic::Time);

    } else if (topic == "BAT") {
        // BAT|phone|82   BAT|rayban|64
        auto p = split(payload, '|', 2);
        if (p.size() < 2) return;
        p[0].toLowerCase();
        if (p[0] == "phone")  s_data.phoneBattery  = clampPercent(p[1]);
        if (p[0] == "rayban") s_data.raybanBattery = clampPercent(p[1]);
        publish(Topic::Battery);

    } else if (topic == "SPOTIFY") {
        // SPOTIFY|<titel>|<artist>|<1=playing,0=paused>
        auto p = split(payload, '|', 3);
        s_data.spotify.title   = p[0];
        s_data.spotify.artist  = p.size() > 1 ? p[1] : "";
        s_data.spotify.playing = p.size() > 2 && p[2].toInt() == 1;
        publish(Topic::Spotify);

    } else if (topic == "UNTIS") {
        // UNTIS|<zeile>\n<zeile>\n...   (Zeile: "08:00-08:45;Mathe;R204;Info")
        s_data.untis.clear();
        for (auto& line : split(payload, '\n', 64)) {
            line.trim();
            if (line.length()) s_data.untis.push_back(line);
        }
        publish(Topic::Untis);

    } else if (topic == "MSG") {
        // MSG|<app>|<titel>|<text>
        auto p = split(payload, '|', 3);
        PhoneMessage m;
        m.app        = p[0];
        m.title      = p.size() > 1 ? p[1] : "";
        m.body       = p.size() > 2 ? p[2] : "";
        m.receivedAt = time(nullptr);
        s_data.messages.push_front(m);
        while (s_data.messages.size() > MAX_MESSAGES) s_data.messages.pop_back();
        publish(Topic::Messages);

    } else if (topic == "MSG_CLEAR") {
        s_data.messages.clear();
        publish(Topic::Messages);

    } else if (topic == "NOTES") {
        // NOTES|<kompletter Text>
        s_data.notes = payload.substring(0, NVS_STRING_MAX);
        settings::putString(KEY_NOTES, s_data.notes);
        publish(Topic::Notes);

    } else if (topic == "GPS") {
        // GPS|48.1371,11.5754
        s_data.location = payload;
        publish(Topic::Location);

    } else if (topic == "SHORTCUT") {
        // SHORTCUT|1|Licht an
        auto p = split(payload, '|', 2);
        int idx = p[0].toInt() - 1;
        if (idx < 0 || idx > 1 || p.size() < 2) return;
        s_data.shortcutLabel[idx] = p[1];
        publish(Topic::Shortcuts);

    } else {
        log_w("Unbekanntes Topic: %s", topic.c_str());
    }
}

}  // namespace store
