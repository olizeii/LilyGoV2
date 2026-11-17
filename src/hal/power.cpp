#include "power.h"

#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#include <driver/gpio.h>
#include <esp_pm.h>
#include <esp_sleep.h>

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
    // Kein Flanken-Interrupt: im Light Sleep gehen Flanken verloren. Der PMU-IRQ-Pin bleibt
    // LOW, bis der Status gelöscht wird -> Pegel abfragen und als Weckquelle nutzen.
    pinMode(PIN_PMU_INT, INPUT_PULLUP);
    (void)onPmuIrq;
    return true;
}

Button pollButton() {
    if (!s_ok || (!s_irq && digitalRead(PIN_PMU_INT) != LOW)) return Button::None;
    s_irq = false;
    pmu.getIrqStatus();
    Button b = pmu.isPekeyLongPressIrq() ? Button::Long
             : pmu.isPekeyShortPressIrq() ? Button::Short : Button::None;
    pmu.clearIrqStatus();
    return b;
}

// --- Stromsparen ---------------------------------------------------------------
static esp_pm_lock_handle_t s_noSleep = nullptr;   // verhindert Light Sleep
static esp_pm_lock_handle_t s_cpuMax  = nullptr;   // 240 MHz (nur bei Display an)
static uint8_t s_reasons = 0;
static bool s_pmOk = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void initSleep() {
#if CONFIG_PM_ENABLE
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "awake", &s_noSleep);
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "ui", &s_cpuMax);
    // Wach bleiben, bis die Gründe gesetzt sind (Display ist beim Start an)
    if (s_noSleep) esp_pm_lock_acquire(s_noSleep);
    if (s_cpuMax) esp_pm_lock_acquire(s_cpuMax);

    esp_pm_config_esp32s3_t cfg = {};
    cfg.max_freq_mhz = 240;
    cfg.min_freq_mhz = 40;          // Quarz-Takt im Leerlauf
    cfg.light_sleep_enable = true;
    esp_err_t err = esp_pm_configure(&cfg);
    s_pmOk = err == ESP_OK;
    if (!s_pmOk) log_e("Power-Management: %s", esp_err_to_name(err));

    // Weckquellen im Light Sleep: Seitentaste (PMU-IRQ) und Touch – beide LOW-aktiv
    gpio_wakeup_enable((gpio_num_t)PIN_PMU_INT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)PIN_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Startsperren durch die echten Gründe ersetzen
    uint8_t r = s_reasons;
    s_reasons = 0;
    keepAwake(r, true);
    if (s_noSleep) esp_pm_lock_release(s_noSleep);
    if (s_cpuMax) esp_pm_lock_release(s_cpuMax);
#endif
}

bool sleepEnabled() { return s_pmOk; }

void keepAwake(uint8_t reason, bool on) {
    portENTER_CRITICAL(&s_mux);
    uint8_t before = s_reasons;
    s_reasons = on ? (before | reason) : (before & ~reason);
    uint8_t after = s_reasons;
    portEXIT_CRITICAL(&s_mux);
    if (!s_noSleep) return;
    if (!before && after)  esp_pm_lock_acquire(s_noSleep);
    if (before && !after)  esp_pm_lock_release(s_noSleep);
    // Volle Taktrate nur fürs Display und Audio
    const uint8_t fast = AWAKE_DISPLAY | AWAKE_AUDIO | AWAKE_VOICE;
    if (!(before & fast) && (after & fast)) esp_pm_lock_acquire(s_cpuMax);
    if ((before & fast) && !(after & fast)) esp_pm_lock_release(s_cpuMax);
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
