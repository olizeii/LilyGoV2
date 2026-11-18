#pragma once
#include <Arduino.h>

// =============================================================================
//  Motion – BMA423 Beschleunigungssensor mit Hardware-Schrittzähler.
//  Der Zähler läuft im Sensor selbst weiter, auch wenn das Display schläft.
//  Tageslogik (Mitternacht, Speichern) liegt in core/activity.
// =============================================================================
namespace motion {

bool begin();        // nach power::begin() (Wire läuft)
bool isOk();

// Schnelle Bewegungen (Gesten): ±8 g bei 200 Hz statt ±2 g/100 Hz. Danach wieder aus.
void setGestureMode(bool on);
bool readAccel(float& x, float& y, float& z);   // in g

uint32_t stepCount(); // Schritte seit dem letzten resetSteps()
void resetSteps();

}  // namespace motion
