#pragma once
#include <Arduino.h>

// =============================================================================
//  Power – AXP2101 PMU: Spannungsschienen, Akku, Power-Taste
// =============================================================================
namespace power {

// Muss VOR dem Display laufen: schaltet Backlight- (ALDO2) und Touch-Rail (ALDO3) ein.
bool begin();

// Im loop() aufrufen. Liefert true, wenn die Seitentaste kurz gedrückt wurde.
bool pollButtonPressed();

int  batteryPercent();   // 0..100, -1 = kein Akku erkannt
bool isCharging();
bool isUsbConnected();
float batteryVoltage();  // Volt

}  // namespace power
