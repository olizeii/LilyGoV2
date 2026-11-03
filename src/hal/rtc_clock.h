#pragma once
#include <time.h>

// =============================================================================
//  RTC – PCF8563 (läuft in UTC) <-> ESP32-Systemzeit (lokal via TZ-String)
// =============================================================================
namespace rtc_clock {

// Liest die RTC und setzt die Systemzeit. Wire muss bereits laufen (power::begin).
void begin(const char* posixTz = "CET-1CEST,M3.5.0,M10.5.0/3");

// Setzt Systemzeit + RTC, z.B. aus der BLE-Zeitsynchronisation (Unix-Epoch, UTC).
void setEpoch(time_t utc);

// Lokale Zeit (bereits TZ-korrigiert)
struct tm now();

bool isValid();  // false, solange die Uhr nie gestellt wurde

}  // namespace rtc_clock
