#pragma once
#include <Arduino.h>

#include "ui/app.h"

// =============================================================================
//  Sound-Gesten – Peitschenhieb oder Pistolen-Wippen mit der Hand -> passender
//  Sound aus dem Lautsprecher. Erkennung über den Beschleunigungssensor (BMA423).
//  Modus: Peitsche / Pistole / Auto (unterscheidet an der Bewegungsrichtung) /
//         Dauerfeuer (schnell wackeln = Salve, still halten = Ruhe).
//  Kreis antippen = Sound zum Ausprobieren. Läuft nur, solange die Kachel offen ist.
// =============================================================================
class GestureApp : public WatchApp {
public:
    const char* name() const override { return "Sound-Gesten"; }
    void create(lv_obj_t* tile) override;
    void onShow() override;
    void onHide() override;

private:
    enum Mode : uint8_t { Whip, Gun, Auto, Burst, MODES };

    void sample();             // 100 Hz: Sensor lesen, Geste erkennen
    void fire(bool gun);
    void setMode(uint8_t m);
    void refresh();

    lv_obj_t* m_circle = nullptr;
    lv_obj_t* m_emoji  = nullptr;
    lv_obj_t* m_label  = nullptr;
    lv_obj_t* m_hint   = nullptr;
    lv_obj_t* m_seg[MODES] = {};
    lv_timer_t* m_timer = nullptr;

    uint8_t m_mode     = Auto;
    float m_g[3]       = {0, 0, 0};   // Schwerkraft-Anteil (Tiefpass)
    bool m_primed      = false;
    uint32_t m_lastFire = 0;
    uint32_t m_flashUntil = 0;
    uint32_t m_count   = 0;
    float m_energy     = 0;           // Dauerfeuer: geglättete Wackel-Stärke
    bool m_shaking     = false;
};
