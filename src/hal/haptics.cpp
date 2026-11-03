#include "haptics.h"

#include <SensorDRV2605.hpp>
#include <Wire.h>

#include "config/pins.h"

namespace haptics {

static SensorDRV2605 s_drv;
static bool s_ok = false;

// Effekte aus der ERM-Bibliothek 1 (DRV2605 Datenblatt, Tabelle 11.2)
static constexpr uint8_t FX_SHARP_CLICK  = 4;
static constexpr uint8_t FX_DOUBLE_CLICK = 10;
static constexpr uint8_t FX_STRONG_BUZZ  = 14;

bool begin() {
    s_ok = s_drv.begin(Wire, DRV2605_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (!s_ok) {
        log_e("DRV2605 nicht gefunden");
        return false;
    }
    s_drv.selectLibrary(1);
    s_drv.setMode(DRV2605_MODE_INTTRIG);
    return true;
}

static void play(uint8_t effect) {
    if (!s_ok) return;
    s_drv.setWaveform(0, effect);
    s_drv.setWaveform(1, 0);  // Ende der Sequenz
    s_drv.run();
}

void tap()     { play(FX_SHARP_CLICK); }
void success() { play(FX_DOUBLE_CLICK); }
void failure() { play(FX_STRONG_BUZZ); }

}  // namespace haptics
