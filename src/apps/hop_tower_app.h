#pragma once
#include "ui/app.h"

// =============================================================================
//  Hüpfturm – ein Würfel springt in einem senkrechten Schacht Etage für Etage
//  nach oben, während von unten Flüssigkeit steigt.
//
//  * Über dem Würfel ist jede Etage zuerst offen und schließt sich dann
//    (Start 0,2–0,8 s nachdem er darunter gelandet ist). Zwei Typen:
//      A  Schiebeplattform – Hälften fahren von den Wänden zur Mitte
//      B  Klappplattform   – Hälften klappen an Wand-Scharnieren hoch
//  * Tippen / Seitentaste = senkrecht springen (feste Höhe, kein Doppelsprung,
//    100 ms Eingabepuffer vor der Landung).
//  * Lücke beim Erreichen der Plattform:  >= Würfelbreite  -> durch
//                                         30–100 %         -> eingeklemmt,
//                                            schnell tippen zum Rausdrücken
//                                         < 30 %           -> abgeprallt
//  * Flüssigkeit steigt immer schneller, holt bei großem Vorsprung auf und
//    braucht nur ~2 s, wenn der Würfel eingesperrt ist.
//  * Tropfen (≈ jede 10. Etage) werden beim Durchspringen eingesammelt.
//    Highscore ("hop_hi") und Tropfen gesamt ("hop_drops") im NVS.
// =============================================================================
class HopTowerApp : public WatchApp {
public:
    const char* name() const override { return "Hüpfturm"; }
    void create(lv_obj_t* tile) override;
    void onShow() override;
    void onHide() override;
    bool onButton() override;

private:
    enum class State : uint8_t { Ready, Running, Dying, Over };
    enum class Mode : uint8_t { Standing, Air, Stuck };
    enum class Plat : uint8_t { Open, Delay, Closing, Closed };

    static constexpr int SLOTS     = 6;   // Ringpuffer der sichtbaren Etagen
    static constexpr int WAVE_COLS = 24;  // Flüssigkeits-Oberfläche
    static constexpr int PARTICLES = 6;   // „Plopp“

    struct Floor {
        int32_t index = -1;
        bool hinge    = false;   // Typ B
        Plat st       = Plat::Open;
        uint16_t delay = 0;
        float t       = 0;       // Fortschritt beim Schließen (Frames)
        float dur     = 60;      // Schließdauer (Frames)
        float open    = 1;       // 1 = offen, 0 = zu
        bool passed   = false;   // Würfel ist durch -> schnell zumachen
        bool fast     = false;
        bool hasDrop  = false;
        bool dropTaken = false;
        lv_obj_t* rectL = nullptr;
        lv_obj_t* rectR = nullptr;
        lv_obj_t* lineL = nullptr;
        lv_obj_t* lineR = nullptr;
        lv_point_t ptsL[2];
        lv_point_t ptsR[2];
        lv_obj_t* drop = nullptr;
    };

    // Ablauf
    void tap();
    void start();
    void stop(bool crashed);
    void showReady();
    void showOver();
    void step();
    void resetWorld();

    // Welt
    Floor& slotFor(int32_t index);
    void initFloor(Floor& f, int32_t index);
    void scheduleClose(int32_t index);
    void updateFloors();
    float gapOf(const Floor& f) const;
    void jump();
    void land(int32_t floorIndex, bool pop);
    void physics();

    // Darstellung
    void render();
    void renderFloor(Floor& f);
    void drawGap(Floor& f, lv_coord_t y, float gap);
    void updateScore();
    void applyColors();
    void setOverlay(bool visible, bool dim);

    lv_obj_t* m_tile = nullptr;
    Floor m_floors[SLOTS];
    lv_obj_t* m_walls[2] = {};
    lv_obj_t* m_jelly = nullptr;
    lv_obj_t* m_eyes[2] = {};
    lv_obj_t* m_wave[WAVE_COLS] = {};
    lv_obj_t* m_part[PARTICLES] = {};
    lv_style_t m_platStyle;   // Rechtecke
    lv_style_t m_lineStyle;   // Klapp-Hälften

    lv_obj_t* m_score = nullptr;
    lv_obj_t* m_overlay = nullptr;
    lv_obj_t* m_ovTitle = nullptr;
    lv_obj_t* m_ovBig = nullptr;
    lv_obj_t* m_ovLine1 = nullptr;
    lv_obj_t* m_ovLine2 = nullptr;
    lv_obj_t* m_ovHint = nullptr;

    lv_timer_t* m_timer = nullptr;
    State m_state = State::Ready;
    Mode m_mode   = Mode::Standing;
    uint32_t m_overAt = 0;

    // Welt: Höhe in px, nach oben positiv. Etage i: Oberkante bei i * SPACING.
    float m_by     = 0;    // Unterkante Würfel
    float m_vy     = 0;
    int32_t m_stand = 0;   // Etage, auf der er steht
    int32_t m_from  = 0;   // Absprung-Etage
    bool m_checked  = false;  // Durchgang der nächsten Plattform schon geprüft
    float m_stuckP  = 0;   // 0..1 Rausdrücken
    float m_cam     = 0;   // Welthöhe am unteren Bildschirmrand
    float m_lava    = 0;
    bool m_trapped  = false;
    float m_trapV   = 0;
    uint8_t m_buffer = 0;  // Eingabepuffer (Frames)
    uint8_t m_dying  = 0;
    uint8_t m_pop    = 0;  // Plopp-Animation
    float m_wob = 0, m_wobV = 0;   // Squash & Stretch (gedämpfte Feder)

    uint32_t m_frame  = 0;
    uint32_t m_points = 0;
    uint32_t m_best   = 0;
    uint32_t m_drops  = 0;   // diese Runde
    uint32_t m_dropsTotal = 0;
    bool m_newRecord = false;
};
