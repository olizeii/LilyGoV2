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
    pmu.setDLDO1Voltage(3300); pmu.disableDLDO1();  // Lautsprecher-Verstärker: nur beim Abspielen an

    // Messungen für die Akku-Anzeige
    pmu.disableTSPinMeasure();          // kein NTC an der Watch
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    pmu.enableSystemVoltageMeasure();

    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    // Seitentaste: kurz = Display, 1,5 s = "Ausschalten?"-Dialog, 10 s = harter Notaus der PMU
    pmu.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S5);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_10S);
    pmu.setLongPressPowerOFF();
    pmu.enableLongPressShutdown();
    pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_1S);  // Einschalten: 1 s halten (nicht aus Versehen)

    // Nur Tasten-IRQs, Rest stumm
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
    pinMode(PIN_PMU_INT, INPUT_PULLUP);
    attachInterrupt(PIN_PMU_INT, onPmuIrq, FALLING);
    return true;
}

Button pollButton() {
    if (!s_ok || !s_irq) return Button::None;
    s_irq = false;
    pmu.getIrqStatus();
    Button b = pmu.isPekeyLongPressIrq() ? Button::Long
             : pmu.isPekeyShortPressIrq() ? Button::Short : Button::None;
    pmu.clearIrqStatus();
    return b;
}

void speaker(bool on) {
    if (!s_ok) return;
    if (on) pmu.enableDLDO1();
    else    pmu.disableDLDO1();
}

void shutdown() {
    if (!s_ok) return;
    // Schienen aus, dann die PMU selbst. Einschalten: Seitentaste 1 s halten.
    pmu.clearIrqStatus();
    pmu.shutdown();
}

int batteryPercent() {
    if (!s_ok || !pmu.isBatteryConnect()) return -1;
    return pmu.getBatteryPercent();
}

bool isCharging()     { return s_ok && pmu.isCharging(); }
bool isUsbConnected() { return s_ok && pmu.isVbusIn(); }
float batteryVoltage() { return s_ok ? pmu.getBattVoltage() / 1000.0f : 0.0f; }

}  // namespace power
