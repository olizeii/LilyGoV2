#pragma once
#include "ui/app.h"

// =============================================================================
//  Stoppuhr – großer runder Knopf: antippen = Start, nochmal = Stopp.
//  Gestoppt: ↺ setzt zurück. Der Ring füllt sich pro Minute einmal.
//  Läuft weiter, wenn man wegwischt oder das Display ausgeht (zählt über millis()).
//  Seitentaste auf dieser Kachel = Start/Stopp.
// =============================================================================
class StopwatchApp : public WatchApp {
public:
    const char* name() const override { return "Stoppuhr"; }
    void create(lv_obj_t* tile) override;
    void onShow() override;
    void onHide() override;
    bool onButton() override {
        toggle();
        return true;
    }

private:
    void toggle();
    void reset();
    void render();
    uint32_t elapsedMs() const;

    lv_obj_t* m_ring   = nullptr;
    lv_obj_t* m_knob   = nullptr;   // runder Start/Stopp-Knopf in der Mitte
    lv_obj_t* m_time   = nullptr;
    lv_obj_t* m_hint   = nullptr;
    lv_obj_t* m_reset  = nullptr;
    lv_timer_t* m_timer = nullptr;

    bool m_running     = false;
    uint32_t m_startMs = 0;      // millis() beim (Wieder-)Start
    uint32_t m_accMs   = 0;      // Summe vorheriger Läufe
};
