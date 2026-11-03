#include "ble_manager.h"

#include <NimBLEDevice.h>

static constexpr const char* SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

// --- NimBLE Callbacks --------------------------------------------------------

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
        BleManager::instance()._setConnected(true);
        // Etwas entspanntere Intervalle (15-30 ms) -> weniger Strom, trotzdem flott
        server->updateConnParams(desc->conn_handle, 12, 24, 0, 400);
    }
    void onDisconnect(NimBLEServer*) override {
        BleManager::instance()._setConnected(false);
        // Advertising startet NimBLE automatisch neu (advertiseOnDisconnect = true)
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        NimBLEAttValue v = c->getValue();
        BleManager::instance()._onRxChunk(std::string(reinterpret_cast<const char*>(v.data()), v.length()));
    }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc*, uint16_t subValue) override {
        // Handy hat Notifications abonniert -> jetzt kann es Commands empfangen
        if (subValue & 0x0001) BleManager::instance()._requestSync();
    }
};

// --- BleManager --------------------------------------------------------------

BleManager& BleManager::instance() {
    static BleManager inst;
    return inst;
}

void BleManager::begin(const char* deviceName) {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(517);  // Android handelt meist >= 247 aus -> kaum Chunking nötig

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* svc = server->createService(SERVICE_UUID);

    NimBLECharacteristic* rx = svc->createCharacteristic(
        RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new RxCallbacks());

    m_tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    m_tx->setCallbacks(new TxCallbacks());

    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();
    log_i("BLE advertising als '%s'", deviceName);
}

void BleManager::_onRxChunk(const std::string& chunk) {
    if (m_partial.size() + chunk.size() > MAX_MESSAGE_LEN) {
        log_w("BLE-Nachricht zu lang, verworfen");
        m_partial.clear();
        return;
    }
    if (!chunk.empty() && chunk.back() == '~') {
        m_partial.append(chunk, 0, chunk.size() - 1);
        return;
    }
    m_partial += chunk;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_inbox.size() >= MAX_QUEUE) m_inbox.pop_front();
    m_inbox.push_back(std::move(m_partial));
    m_partial.clear();
}

void BleManager::loop() {
    if (m_connected != m_lastConnected) {
        m_lastConnected = m_connected;
        if (!m_connected) m_syncRequested = false;
        if (m_onConnection) m_onConnection(m_lastConnected);
    }

    if (m_syncRequested) {
        m_syncRequested = false;
        send("SYNC", "all");  // WatchBridge antwortet mit TIME, BAT, MEDIA, UNTIS, NOTES ...
    }

    // Queue unter Lock leeren, Handler ohne Lock aufrufen
    std::deque<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_inbox);
    }
    for (auto& msg : batch) {
        if (m_onMessage) m_onMessage(String(msg.c_str()));
    }
}

bool BleManager::send(const String& cmd, const String& arg) {
    if (!m_connected || !m_tx) return false;
    String out = arg.length() ? cmd + "|" + arg : cmd;
    m_tx->setValue(reinterpret_cast<const uint8_t*>(out.c_str()), out.length());
    m_tx->notify();
    return true;
}
