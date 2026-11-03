#pragma once
#include <Arduino.h>

#include <deque>
#include <functional>
#include <vector>

// =============================================================================
//  DataStore – zentrale Daten vom Handy (BLE) + Pub/Sub für die UI.
//  Läuft ausschließlich im LVGL/loop()-Kontext -> Listener dürfen LVGL aufrufen.
// =============================================================================

enum class Topic : uint8_t {
    Time,
    Battery,      // Watch-, Handy-, Ray-Ban-Akku
    Spotify,
    Untis,
    Messages,
    Notes,
    Location,
    Connection,   // BLE verbunden / getrennt
    Shortcuts,
    Activity,     // Schritte (lokal, BMA423)
    Count
};

struct SpotifyState {
    String title;
    String artist;
    bool playing = false;
};

struct PhoneMessage {
    String app;
    String title;
    String body;
    uint32_t receivedAt = 0;  // Unix-Zeit
};

struct WatchData {
    int watchBattery  = -1;   // -1 = unbekannt
    bool watchCharging = false;
    int phoneBattery  = -1;
    int raybanBattery = -1;

    SpotifyState spotify;
    std::vector<String> untis;          // Rohzeilen, Format siehe README
    std::deque<PhoneMessage> messages;  // neueste zuerst
    String notes;
    String location;                    // "48.1371, 11.5754"
    String shortcutLabel[2] = {"Shortcut 1", "Shortcut 2"};
    bool bleConnected = false;
};

namespace store {

static constexpr size_t MAX_MESSAGES = 20;

using Listener = std::function<void()>;

void begin();  // lädt persistente Daten (Notizen) aus dem NVS

WatchData& data();
void subscribe(Topic topic, Listener listener);
void publish(Topic topic);

// Parst eine komplette Protokoll-Nachricht "TOPIC|payload" vom Handy.
void handleMessage(const String& msg);

}  // namespace store
