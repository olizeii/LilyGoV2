#pragma once
#include <lvgl.h>

// =============================================================================
//  WatchApp – Basisklasse für jede Kachel im Raster.
//
//  Lebenszyklus:
//    create(tile)  einmalig beim Aufbau, `tile` ist die 240x240 Tileview-Kachel
//    onShow()      Kachel ist jetzt aktiv (nach dem Wischen)
//    onTick()      1x pro Sekunde, NUR solange sichtbar und Display an
//    onHide()      Kachel wurde verlassen
//
//  Daten vom Handy: im create() per store::subscribe(Topic::X, ...) abonnieren.
// =============================================================================
class WatchApp {
public:
    virtual ~WatchApp() = default;

    virtual const char* name() const = 0;
    virtual void create(lv_obj_t* tile) = 0;

    virtual void onShow() {}
    virtual void onHide() {}
    virtual void onTick() {}
};
