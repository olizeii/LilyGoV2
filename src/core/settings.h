#pragma once
#include <Arduino.h>

// =============================================================================
//  Settings – dauerhafte Zustände im NVS (Preferences.h)
//  Schreibt nur, wenn sich der Wert wirklich ändert (Flash-Verschleiß).
// =============================================================================
namespace settings {

void begin();

// --- Typisierte Keys der Firmware ---
uint8_t lastWatchface();
void    setLastWatchface(uint8_t row);

bool darkMode();
void setDarkMode(bool dark);

uint8_t brightness();
void    setBrightness(uint8_t level);

// --- Generischer Zugriff für Apps (Key max. 15 Zeichen!) ---
uint8_t getU8(const char* key, uint8_t def);
void    putU8(const char* key, uint8_t value);
uint32_t getU32(const char* key, uint32_t def);
void     putU32(const char* key, uint32_t value);
String  getString(const char* key, const String& def = "");
void    putString(const char* key, const String& value);

}  // namespace settings
