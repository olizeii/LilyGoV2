#pragma once
#include <Arduino.h>

// =============================================================================
//  Activity – Tages-Schritte auf Basis des BMA423-Hardwarezählers.
//
//  * Um Mitternacht (lokale Zeit) wird der Zähler zurückgesetzt.
//  * Der Tagesstand wird alle 5 min im NVS gesichert -> ein Neustart verliert
//    höchstens die letzten Minuten.
//  * Änderungen werden über store::publish(Topic::Activity) verteilt.
// =============================================================================
namespace activity {

void begin();  // nach rtc_clock::begin() und store::begin()
void loop();   // in loop(), auch während das Display schläft

bool     available();   // Sensor gefunden?
uint32_t steps();       // heute
uint32_t goal();        // Tagesziel (Standard 10.000)
uint32_t distanceM();   // geschätzt, Schrittlänge 0,75 m
uint32_t kcal();        // grobe Schätzung, 0,04 kcal pro Schritt

}  // namespace activity
