// =============================================================================
//  LilyGo T-Watch-S3 – Apple-Style Firmware
//
//  Boot-Reihenfolge:
//    1. PMU (Strom für Backlight + Touch)   4. Theme + Apps + Tileview
//    2. RTC -> Systemzeit                    5. BLE-Server (Tasker-Bridge)
//    3. Display/Touch + LVGL                 (+ Vibration, Schrittzähler)
//
//  Threading: Alles LVGL-bezogene läuft NUR in loop(). BLE-Daten kommen über die
//  Queue im BleManager und werden in loop() an den DataStore verteilt.
// =============================================================================
#include <Arduino.h>
#include <lvgl.h>

#include "apps/apps.h"
#include "ble_manager.h"
#include "core/activity.h"
#include "core/data_store.h"
#include "core/settings.h"
#include "hal/display.h"
#include "hal/haptics.h"
#include "hal/power.h"
#include "hal/rtc_clock.h"
#include "ui/ai_screen.h"
#include "ui/banner.h"
#include "ui/map_screen.h"
#include "ui/theme.h"
#include "ui/ui_core.h"

static constexpr const char* BLE_NAME           = "T-Watch-S3";
static constexpr uint32_t SCREEN_TIMEOUT_MS     = 15000;
static constexpr uint32_t BATTERY_POLL_MS       = 10000;

static void wakeUp() {
    ui::onWake();     // erst aufs Watchface springen ...
    display::wake();  // ... dann zeichnen + Licht an
}

static void sleepDisplay() {
    aiscreen::close();  // Assistent nicht im Hintergrund weiterlaufen lassen
    display::sleep();
}

static void pollWatchBattery() {
    static uint32_t last = 0;
    if (last && millis() - last < BATTERY_POLL_MS) return;
    last = millis();

    WatchData& d  = store::data();
    int pct       = power::batteryPercent();
    bool charging = power::isCharging();
    if (pct != d.watchBattery || charging != d.watchCharging) {
        d.watchBattery  = pct;
        d.watchCharging = charging;
        store::publish(Topic::Battery);
    }
}

void setup() {
    Serial.begin(115200);

    settings::begin();
    power::begin();
    rtc_clock::begin();

    lv_disp_t* disp = display::begin(settings::brightness());
    theme::begin(disp, settings::darkMode());

    store::begin();
    pollWatchBattery();
    haptics::begin();
    activity::begin();

    apps::registerAll();
    ui::build();
    banner::begin();
    mapscreen::begin();
    aiscreen::begin();

    BleManager& ble = BleManager::instance();
    ble.onMessage([](const String& msg) { store::handleMessage(msg); });
    ble.onConnection([](bool connected) {
        store::data().bleConnected = connected;
        store::publish(Topic::Connection);
    });
    ble.begin(BLE_NAME);
}

void loop() {
    BleManager::instance().loop();

    // Seitentaste: Display an/aus
    if (power::pollButtonPressed()) {
        if (display::isAsleep())      wakeUp();
        else if (aiscreen::isOpen())    aiscreen::close();
        else if (!ui::dispatchButton()) sleepDisplay();  // z.B. Spiel: Taste = springen
    }
    if (display::consumeTouchWake()) wakeUp();

    if (!display::isAsleep() && lv_disp_get_inactive_time(nullptr) > SCREEN_TIMEOUT_MS) {
        sleepDisplay();
    }

    pollWatchBattery();
    activity::loop();  // läuft auch bei Display aus weiter
    ui::loop(!display::isAsleep());
    lv_timer_handler();

    delay(display::isAsleep() ? 30 : 5);
}
