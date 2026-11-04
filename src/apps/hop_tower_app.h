#pragma once
#include "ui/app.h"

// =============================================================================
//  Hüpfturm – vertikales Endlos-Sprungspiel.
//
//  * Jedes Stockwerk ist ein Balken mit einer Lücke, die hin- und herwandert.
//  * Tippen = springen. Nur wer unter der Lücke abspringt, kommt durch –
//    sonst prallt der Tropfen ab und fällt zurück. Hinter ihm schließt sich
//    der Balken (die Hälften fahren zusammen).
//  * Von unten steigt Lava, immer schneller. Berührt sie den Tropfen: vorbei.
//  * Punkte = höchstes erreichtes Stockwerk, Rekord im NVS ("hop_hi").
//  * Während des Spiels ist das Wischen gesperrt (wie beim Runner).
// =============================================================================
class HopTowerApp : public WatchApp {
public:
    const char* name() const override { return "Hüpfturm"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { showReady(); }
    void onHide() override { stop(false); }

private:
    enum class State : uint8_t { Ready, Running, Over };

    static constexpr int SLOTS = 6;  // sichtbare Stockwerke (Ringpuffer)

    struct Floor {
        lv_obj_t* left  = nullptr;
        lv_obj_t* right = nullptr;
        int32_t index   = -1;   // welches Stockwerk dieser Slot gerade zeigt
        float gapX      = 0;    // linke Kante der Lücke
        float gapW      = 0;    // 0 = keine Lücke (Boden)
        float gapV      = 0;    // px/Frame
        bool closed     = false;  // Tropfen ist durch -> Lücke fährt zu
    };

    void onPress(bool clicked);
    void start();
    void stop(bool crashed);
    void showReady();
    void step();
    void resetWorld();

    Floor& slotFor(int32_t floorIndex);
    void initFloor(Floor& f, int32_t floorIndex);
    bool insideGap(const Floor& f) const;
    void render();
    void updateScore();
    void applyColors();

    lv_obj_t* m_tile   = nullptr;
    lv_obj_t* m_player = nullptr;
    lv_obj_t* m_eyes[2] = {};
    lv_obj_t* m_lava   = nullptr;
    lv_obj_t* m_lavaTop = nullptr;
    Floor m_floors[SLOTS];

    lv_obj_t* m_score = nullptr;
    lv_obj_t* m_best  = nullptr;
    lv_obj_t* m_title = nullptr;
    lv_obj_t* m_hint  = nullptr;

    lv_timer_t* m_timer = nullptr;
    State m_state       = State::Ready;
    uint32_t m_overAt   = 0;

    // Welt: Höhe in px, nach oben positiv. Stockwerk i liegt bei i * SPACING.
    float m_py      = 0;     // Unterkante Tropfen
    float m_vy      = 0;
    int32_t m_stand = 0;     // Stockwerk, auf dem er steht (-1 = in der Luft)
    float m_cam     = 0;     // Welthöhe am unteren Bildschirmrand
    float m_lavaH   = 0;     // Lava-Oberfläche
    uint8_t m_squash = 0;    // Frames Wabbel-Animation
    uint8_t m_jumpBuffer = 0;
    uint32_t m_frame  = 0;
    uint32_t m_points = 0;
    uint32_t m_best_  = 0;
};
