#include "power.h"

#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#include "config/pins.h"

namespace power {

static XPowersPMU pmu;
static bool s_ok = false;
static volatile bool s_irq = false;

static void IRAM_ATTR onPmuIrq() { s_irq = true; }

bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    s_ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (!s_ok) {
        log_e("AXP2101 nicht gefunden");
        return false;
    }

    // Schienen laut T-Watch-S3 Schaltplan
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();   // RTC VBAT
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();   // TFT Backlight
    pmu.setALDO3Voltage(3300); pmu.enableALDO3();   // Touch
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();   // DRV2605 (Vibration) Enable
    pmu.disableALDO4();                             // LoRa – ungenutzt, spart Strom

    // Messungen für die Akku-Anzeige
    pmu.disableTSPinMeasure();          // kein NTC an der Watch
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    pmu.enableSystemVoltageMeasure();

    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);

    // Nur Tasten-IRQs, Rest stumm
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    pinMode(PIN_PMU_INT, INPUT_PULLUP);
    attachInterrupt(PIN_PMU_INT, onPmuIrq, FALLING);
    return true;
}

bool pollButtonPressed() {
    if (!s_ok || !s_irq) return false;
    s_irq = false;
    pmu.getIrqStatus();
    bool pressed = pmu.isPekeyShortPressIrq();
    pmu.clearIrqStatus();
    return pressed;
}

int batteryPercent() {
    if (!s_ok || !pmu.isBatteryConnect()) return -1;
    return pmu.getBatteryPercent();
}

bool isCharging()     { return s_ok && pmu.isCharging(); }
bool isUsbConnected() { return s_ok && pmu.isVbusIn(); }
float batteryVoltage() { return s_ok ? pmu.getBattVoltage() / 1000.0f : 0.0f; }

}  // namespace power
