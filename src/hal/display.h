#pragma once
#include <lvgl.h>

// =============================================================================
//  Display – ST7789 + FT6336 via LovyanGFX, als LVGL-Display/Indev registriert.
//  Enthält Backlight und Sleep/Wake. Der erste Touch im Sleep weckt nur auf und
//  wird nicht an LVGL weitergegeben (kein versehentlicher Klick).
// =============================================================================
namespace display {

// Voraussetzung: power::begin() lief (Backlight- und Touch-Rail an).
lv_disp_t* begin(uint8_t brightness);

void setBrightness(uint8_t level);

void sleep();
void wake();
bool isAsleep();

// true (einmalig), wenn im Sleep der Touchscreen berührt wurde
bool consumeTouchWake();

}  // namespace display
