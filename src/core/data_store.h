#pragma once
#include <Arduino.h>

#include <deque>
#include <functional>
#include <vector>

// =============================================================================
//  DataStore – zentrale Daten vom Handy (BLE) + Pub/Sub für die UI.
//  Läuft ausschließlich im LVGL/loop()-Kontext -> Listener dürfen LVGL aufrufen.
//  Protokoll: siehe README ("BLE-Protokoll").
// =============================================================================

enum class Topic : uint8_t {
    Time,
    Battery,      // Watch-, Handy-, Ray-Ban-Akku
    Media,        // Spotify / aktive Medien-Session
    Untis,
    Messages,     // Liste geändert
    Alert,        // neue Benachrichtigung eingetroffen (für Banner + Vibration)
    Notes,
    Location,
    Nav,          // Navigation (Google Maps) gestartet / geändert / beendet
    Map,          // neues Kartenbild komplett empfangen
    Connection,   // BLE verbunden / getrennt
    Shortcuts,
    Activity,     // Schritte (lokal, BMA423)
    Count
};

struct MediaState {
    bool active   = false;  // gibt es überhaupt eine Medien-Session?
    bool playing  = false;
    String title;
    String artist;
    String app;             // "Spotify", "YouTube Music" ...
    uint32_t posSec = 0;    // Position beim Empfang
    uint32_t durSec = 0;
    uint32_t rxMs   = 0;    // millis() beim Empfang -> Position lokal hochzählen

    uint32_t positionNow() const {
        if (!playing) return posSec;
        uint32_t p = posSec + (millis() - rxMs) / 1000;
        return durSec && p > durSec ? durSec : p;
    }
};

struct Lesson {
    String start, end;      // "08:00"
    String subject;
    String room;
    String teacher;
    String info;            // Vertretungstext o.ä.
    char status = 'N';      // N normal, C entfällt, S Vertretung/geändert, E Prüfung
};

struct PhoneMessage {
    String key;             // Android-Notification-Key (zum Löschen)
    String app;
    String title;
    String body;
    uint32_t receivedAt = 0;  // Unix-Zeit
};

struct Note {
    String id;
    String title;
    String body;
};

struct NavState {
    bool active = false;
    String maneuver;        // left, right, slight_left, ..., uturn, roundabout, straight, arrive
    String distance;        // "200 m"
    String instruction;     // "Rechts abbiegen auf Hauptstraße"
    String eta;             // "12 min · 3,4 km · 14:32"
};

struct MapImage {
    uint8_t* data   = nullptr;  // LVGL INDEXED_4BIT: 16x ARGB8888 Palette + Pixel (PSRAM)
    uint32_t size   = 0;
    uint16_t w      = 0;
    uint16_t h      = 0;
    uint32_t version = 0;       // zählt bei jedem fertigen Bild hoch
};

struct WatchData {
    int watchBattery   = -1;   // -1 = unbekannt
    bool watchCharging = false;
    int phoneBattery   = -1;
    bool phoneCharging = false;
    int raybanBattery  = -1;

    MediaState media;
    String untisTitle;                  // z.B. "Heute · Di 4. Nov."
    std::vector<Lesson> untis;
    std::deque<PhoneMessage> messages;  // neueste zuerst
    PhoneMessage alert;                 // zuletzt eingetroffene (für Banner)
    std::vector<Note> notes;
    String location;                    // "48.1371, 11.5754"
    String place;                       // "Marienplatz, München"
    NavState nav;
    MapImage map;
    String shortcutLabel[2] = {"Kurzbefehl 1", "Kurzbefehl 2"};
    bool bleConnected = false;
};

namespace store {

static constexpr size_t MAX_MESSAGES = 30;

using Listener = std::function<void()>;

void begin();  // lädt Notizen aus dem Flash (LittleFS)

WatchData& data();
void subscribe(Topic topic, Listener listener);
void publish(Topic topic);

// Zusätzliche Topics (z.B. KI-Assistent): Handler gibt true zurück, wenn er das Topic kennt.
using TopicHandler = std::function<bool(const String& topic, const String& payload)>;
void addHandler(TopicHandler handler);

// Parst eine komplette Protokoll-Nachricht "TOPIC|payload" vom Handy.
void handleMessage(const String& msg);

// Befehl an das Handy senden ("CMD|arg"). false = nicht verbunden.
bool sendToPhone(const String& cmd, const String& arg = "");

// Benachrichtigung lokal entfernen und dem Handy sagen, dass sie weg kann.
void dismissMessage(const String& key);

}  // namespace store
