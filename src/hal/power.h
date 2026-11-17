#pragma once
#include <Arduino.h>

// =============================================================================
//  Power – AXP2101 PMU: Spannungsschienen, Akku, Power-Taste
// =============================================================================
namespace power {

// Muss VOR dem Display laufen: schaltet Backlight- (ALDO2) und Touch-Rail (ALDO3) ein.
bool begin();

enum class Button { None, Short, Long };

// Im loop() aufrufen: kurz gedrückt / 1,5 s gehalten.
Button pollButton();

// Versorgung des Lautsprecher-Verstärkers (MAX98357, PMU-Schiene DLDO1)
void speaker(bool on);

// --- Stromsparen (ESP-IDF Power-Management) -----------------------------------
// Ohne Sperre schläft die CPU automatisch (Light Sleep), BLE bleibt verbunden.
// Solange irgendein Grund gesetzt ist, bleibt sie wach.
enum Awake : uint8_t {
    AWAKE_DISPLAY = 1,   // Display an: volle Taktrate, flüssige UI
    AWAKE_USB     = 2,   // am Kabel: USB-Seriell/Flashen soll erreichbar bleiben
    AWAKE_AUDIO   = 4,   // Studio spielt / nimmt auf
    AWAKE_VOICE   = 8,   // Sprachassistent nimmt auf
};
void initSleep();                    // nach BLE-Start aufrufen
void keepAwake(uint8_t reason, bool on);
bool sleepEnabled();                 // Power-Management aktiv?

// Uhr komplett ausschalten (wie "Ausschalten" am iPhone). Einschalten: Taste 1 s halten.
void shutdown();

int  batteryPercent();   // 0..100, -1 = kein Akku erkannt
bool isCharging();
bool isUsbConnected();
float batteryVoltage();  // Volt

}  // namespace power
