#pragma once
#include <lvgl.h>

#include "ui/app.h"

// =============================================================================
//  UI-Core – 2D-Raster (lv_tileview), App-Registrierung, Watchface-Gedächtnis
//
//      X=0      X=1 (Start)   X=2        X=3        X=4
//  Y=0 TicTac   Digital       Untis      Widget 1   IR-Remote
//  Y=1 Runner   Analog        Spotify    Notizen    Hack
//  Y=2          Modern        Nachr.     Widget 2
//  Y=3          Info
//
//  Wischrichtungen werden automatisch aus den Nachbarn im Raster berechnet.
// =============================================================================
namespace ui {

constexpr uint8_t COL_WATCHFACES  = 1;
constexpr uint8_t WATCHFACE_COUNT = 3;   // Y=0..2 sind Watchfaces (Y=3 = Info)

// Vor build() aufrufen. Die App muss bis zum Programmende leben.
void registerApp(uint8_t col, uint8_t row, WatchApp* app);

// Baut Tileview + alle registrierten Kacheln und springt aufs letzte Watchface.
void build();

// Im loop(): verteilt den 1-Hz-Tick an die sichtbare App.
void loop(bool displayOn);

void showTile(uint8_t col, uint8_t row, bool animate = false);
void showWatchface(bool animate = false);  // zuletzt aktives Watchface (NVS)

void onWake();  // Display wieder an -> immer zurück aufs Watchface

lv_obj_t* tileview();

}  // namespace ui
