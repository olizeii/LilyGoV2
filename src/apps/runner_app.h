#pragma once
#include "ui/app.h"

// =============================================================================
//  Runner – Endlos-Laufspiel. Tippen = springen, Säulen ausweichen.
//
//  * 50 fps über einen eigenen lv_timer, nur solange das Spiel läuft.
//  * Während des Laufens ist das Wischen zwischen Kacheln gesperrt.
//  * Tempo steigt mit den Punkten, ab 150 Punkten kommen Doppelsäulen.
//  * Rekord wird im NVS gespeichert ("run_hi").
// =============================================================================
class RunnerApp : public WatchApp {
public:
    const char* name() const override { return "Runner"; }
    void create(lv_obj_t* tile) override;
    void onShow() override { showReady(); }
    void onHide() override { stop(false); }

private:
    enum class State : uint8_t { Ready, Running, Over };

    struct Obstacle {
        lv_obj_t* obj = nullptr;
        float x       = 0;
        lv_coord_t w  = 0;
        lv_coord_t h  = 0;
        bool active   = false;
    };

    static constexpr int MAX_OBSTACLES = 5;
    static constexpr int DASHES        = 6;
    static constexpr int CLOUDS        = 3;

    void onPress(bool clicked);
    void start();
    void stop(bool crashed);
    void showReady();
    void step();
    void spawn(float x);
    void layoutPlayer();
    void updateScore();
    void applyColors();

    lv_obj_t* m_tile    = nullptr;
    lv_obj_t* m_player  = nullptr;
    lv_obj_t* m_legs[2] = {};
    lv_obj_t* m_ground  = nullptr;
    lv_point_t m_groundPts[2];
    lv_obj_t* m_dashes[DASHES] = {};
    float m_dashX[DASHES]      = {};
    lv_obj_t* m_clouds[CLOUDS] = {};
    float m_cloudX[CLOUDS]     = {};
    Obstacle m_obs[MAX_OBSTACLES];

    lv_obj_t* m_score = nullptr;
    lv_obj_t* m_best  = nullptr;
    lv_obj_t* m_title = nullptr;
    lv_obj_t* m_hint  = nullptr;

    lv_style_t m_solid;  // Säulen
    lv_style_t m_faint;  // Boden-Striche, Wolken

    lv_timer_t* m_timer = nullptr;
    State m_state       = State::Ready;
    uint32_t m_overAt   = 0;

    float m_y        = 0;  // Höhe über dem Boden (px)
    float m_vy       = 0;
    float m_speed    = 0;
    float m_distance = 0;
    float m_nextGap  = 0;
    uint8_t m_jumpBuffer = 0;
    uint32_t m_frame = 0;
    uint32_t m_points = 0;
    uint32_t m_best_  = 0;
};
