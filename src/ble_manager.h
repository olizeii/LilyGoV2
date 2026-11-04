#pragma once
#include <Arduino.h>

#include <deque>
#include <functional>
#include <mutex>
#include <string>

class NimBLECharacteristic;

// =============================================================================
//  BleManager – NimBLE GATT-Server als Brücke zu Tasker (Android).
//
//  Service = Nordic UART Service (NUS), damit sofort mit Standard-Apps
//  (z.B. "Serial Bluetooth Terminal") testbar:
//    RX 6E400002-... (WRITE)   Handy -> Uhr  : "TOPIC|payload"
//    TX 6E400003-... (NOTIFY)  Uhr  -> Handy : "CMD|argument"
//
//  Lange Nachrichten dürfen in mehreren Writes kommen: endet ein Write auf '~',
//  folgt noch ein Teil. Der erste Write ohne '~' am Ende schließt die Nachricht ab.
//
//  Threading: NimBLE-Callbacks laufen im BLE-Host-Task. Nachrichten werden dort nur
//  in eine Queue gelegt; loop() liefert sie im LVGL-Kontext an den Handler aus.
// =============================================================================
class BleManager {
public:
    using MessageHandler    = std::function<void(const String& msg)>;
    using ConnectionHandler = std::function<void(bool connected)>;

    static BleManager& instance();

    void begin(const char* deviceName);

    // Im Arduino-loop() aufrufen (gleicher Thread wie LVGL!)
    void loop();

    void onMessage(MessageHandler h)       { m_onMessage = std::move(h); }
    void onConnection(ConnectionHandler h) { m_onConnection = std::move(h); }

    // Sendet "CMD|arg" per Notify an das Handy. false = nicht verbunden.
    bool send(const String& cmd, const String& arg = "");

    bool isConnected() const { return m_connected; }

    // Nutzdaten pro Notify (ATT-MTU - 3). 20 solange nichts ausgehandelt ist.
    uint16_t payloadSize() const;

    // Kürzere Verbindungsintervalle (7,5–15 ms) z.B. fürs Audio-Streaming.
    void setFastLink(bool fast);

    // --- intern, von den NimBLE-Callbacks aufgerufen ---
    void _onRxChunk(const std::string& chunk);
    void _setConnected(bool c) { m_connected = c; }
    void _setConnHandle(uint16_t h) { m_connHandle = h; }
    void _requestSync()        { m_syncRequested = true; }

private:
    BleManager() = default;

    static constexpr size_t MAX_MESSAGE_LEN = 8192;
    static constexpr size_t MAX_QUEUE       = 64;  // Kartenbild = ~12 Nachrichten am Stück

    NimBLECharacteristic* m_tx = nullptr;

    std::mutex m_mutex;
    std::mutex m_txMutex;  // send() kommt auch aus dem Audio-Task
    uint16_t m_connHandle = 0xFFFF;
    std::deque<std::string> m_inbox;
    std::string m_partial;  // nur im BLE-Task benutzt

    volatile bool m_connected     = false;
    volatile bool m_syncRequested = false;
    bool m_lastConnected          = false;

    MessageHandler m_onMessage;
    ConnectionHandler m_onConnection;
};
