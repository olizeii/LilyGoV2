#pragma once
#include <Arduino.h>

// =============================================================================
//  Haptics – DRV2605 Vibrationsmotor (Rail BLDO2, siehe power.cpp).
//  Alle Aufrufe sind nicht-blockierend und gefahrlos, falls der Chip fehlt.
// =============================================================================
namespace haptics {

bool begin();

void tap();      // kurzer, leichter Klick (Tastendruck)
void success();  // Doppel-Klick (gewonnen, Ziel erreicht)
void failure();  // kräftiges Brummen (verloren)

}  // namespace haptics
