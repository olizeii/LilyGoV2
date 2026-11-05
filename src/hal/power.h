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

// Uhr komplett ausschalten (wie "Ausschalten" am iPhone). Einschalten: Taste 1 s halten.
void shutdown();

int  batteryPercent();   // 0..100, -1 = kein Akku erkannt
bool isCharging();
bool isUsbConnected();
float batteryVoltage();  // Volt

}  // namespace power
