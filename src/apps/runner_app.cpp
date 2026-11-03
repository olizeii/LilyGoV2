#include "runner_app.h"

#include <esp_random.h>

#include "core/settings.h"
#include "hal/display.h"
#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/ui_core.h"

namespace {

constexpr const char* KEY_BEST = "run_hi";

constexpr uint32_t FRAME_MS   = 20;    // 50 fps
constexpr lv_coord_t W        = 240;
constexpr lv_coord_t GROUND_Y = 186;   // Oberkante Boden
constexpr lv_coord_t PLAYER_X = 34;
constexpr lv_coord_t PW       = 24;    // Spielfigur
constexpr lv_coord_t PH       = 26;
constexpr lv_coord_t LEG_H    = 6;

constexpr float JUMP_V      = 8.2f;    // px/Frame -> ~74 px Sprunghöhe
constexpr float GRAVITY     = 0.45f;
constexpr float SPEED_START = 3.6f;    // px/Frame = 180 px/s
constexpr float SPEED_MAX   = 8.0f;
constexpr float SPEED_GAIN  = 0.004f;  // pro Punkt
constexpr float PX_PER_POINT = 10.0f;

int rnd(int lo, int hi) { return lo + int(esp_random() % uint32_t(hi - lo + 1)); }

lv_obj_t* rect(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_coord_t radius) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

}  // namespace

// -----------------------------------------------------------------------------
//  Aufbau
// -----------------------------------------------------------------------------
void RunnerApp::create(lv_obj_t* tile) {
    m_tile = tile;
    m_best_ = settings::getU32(KEY_BEST, 0);

    lv_style_init(&m_solid);
    lv_style_init(&m_faint);
    applyColors();
    theme::onChange([this] { applyColors(); });

    // Wolken (Parallaxe, langsam)
    for (int i = 0; i < CLOUDS; i++) {
        m_clouds[i] = rect(tile, rnd(26, 40), 10, LV_RADIUS_CIRCLE);
        lv_obj_add_style(m_clouds[i], &m_faint, 0);
        m_cloudX[i] = 40 + i * 90;
        lv_obj_set_pos(m_clouds[i], lv_coord_t(m_cloudX[i]), 70 + i * 22);
    }

    // Boden
    m_groundPts[0] = {0, GROUND_Y};
    m_groundPts[1] = {W, GROUND_Y};
    m_ground = lv_line_create(tile);
    lv_obj_add_style(m_ground, theme::lineSecondary(), 0);
    lv_line_set_points(m_ground, m_groundPts, 2);
    for (int i = 0; i < DASHES; i++) {
        m_dashes[i] = rect(tile, rnd(6, 14), 2, 1);
        lv_obj_add_style(m_dashes[i], &m_faint, 0);
        m_dashX[i] = i * (W / DASHES) + rnd(0, 20);
        lv_obj_set_pos(m_dashes[i], lv_coord_t(m_dashX[i]), GROUND_Y + 8 + (i % 3) * 8);
    }

    // Säulen
    for (auto& o : m_obs) {
        o.obj = rect(tile, 16, 30, 6);
        lv_obj_add_style(o.obj, &m_solid, 0);
        lv_obj_add_flag(o.obj, LV_OBJ_FLAG_HIDDEN);
    }

    // Spielfigur: orangener Körper, Auge, zwei Beine
    for (int i = 0; i < 2; i++) {
        m_legs[i] = rect(tile, 6, LEG_H + 2, 3);
        lv_obj_set_style_bg_color(m_legs[i], theme::tint(theme::Tint::Orange), 0);
    }
    m_player = rect(tile, PW, PH, 10);
    lv_obj_set_style_bg_color(m_player, theme::tint(theme::Tint::Orange), 0);
    lv_obj_t* eye = rect(m_player, 9, 9, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(eye, lv_color_white(), 0);
    lv_obj_set_pos(eye, PW - 12, 5);
    lv_obj_t* pupil = rect(eye, 4, 4, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(pupil, lv_color_black(), 0);
    lv_obj_set_pos(pupil, 4, 2);

    // Anzeigen
    m_best = theme::label(tile, theme::textCaption(), "");
    lv_obj_align(m_best, LV_ALIGN_TOP_LEFT, 18, 14);
    m_score = theme::label(tile, theme::textHeadline(), "0");
    lv_obj_align(m_score, LV_ALIGN_TOP_RIGHT, -18, 12);

    m_title = theme::label(tile, theme::textTitle(), "");
    m_hint  = theme::label(tile, theme::textCaption(), "");

    // Eingabe: Springen auf PRESSED (ohne Verzögerung), Start/Neustart erst auf
    // CLICKED – so bleibt Wischen aus der Kachel heraus möglich.
    lv_obj_add_event_cb(
        tile,
        [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;
            static_cast<RunnerApp*>(lv_event_get_user_data(e))->onPress(code == LV_EVENT_CLICKED);
        },
        LV_EVENT_ALL, this);

    m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<RunnerApp*>(t->user_data)->step(); },
                              FRAME_MS, this);
    lv_timer_pause(m_timer);
    showReady();
}

void RunnerApp::applyColors() {
    const theme::Palette& p = theme::colors();
    lv_style_set_bg_color(&m_solid, p.text);
    lv_style_set_bg_color(&m_faint, p.surfaceAlt);
    lv_obj_report_style_change(&m_solid);
    lv_obj_report_style_change(&m_faint);
}

// -----------------------------------------------------------------------------
//  Zustände
// -----------------------------------------------------------------------------
void RunnerApp::showReady() {
    if (m_state == State::Running) return;
    m_state = State::Ready;
    m_y = m_vy = 0;
    m_frame = 0;
    for (auto& o : m_obs) {
        o.active = false;
        lv_obj_add_flag(o.obj, LV_OBJ_FLAG_HIDDEN);
    }
    layoutPlayer();

    lv_label_set_text(m_title, "Runner");
    lv_obj_align(m_title, LV_ALIGN_CENTER, 0, -46);
    lv_label_set_text(m_hint, "Tippen zum Springen");
    lv_obj_align(m_hint, LV_ALIGN_CENTER, 0, -18);
    lv_obj_clear_flag(m_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_hint, LV_OBJ_FLAG_HIDDEN);

    m_points = 0;
    updateScore();
}

void RunnerApp::onPress(bool clicked) {
    switch (m_state) {
        case State::Ready:
            if (clicked) start();
            break;
        case State::Running:
            if (clicked) break;
            if (m_y <= 0.5f) {
                m_vy = JUMP_V;
            } else {
                m_jumpBuffer = 8;  // kurz vor der Landung getippt -> gleich wieder springen
            }
            break;
        case State::Over:
            if (clicked && millis() - m_overAt > 600) {  // kein versehentlicher Neustart
                m_state = State::Ready;
                showReady();
                start();
            }
            break;
    }
}

void RunnerApp::start() {
    m_state    = State::Running;
    m_speed    = SPEED_START;
    m_distance = 0;
    m_points   = 0;
    m_y        = 0;
    m_vy       = JUMP_V;  // der Start-Tipp ist gleich der erste Sprung
    m_jumpBuffer = 0;
    for (auto& o : m_obs) {
        o.active = false;
        lv_obj_add_flag(o.obj, LV_OBJ_FLAG_HIDDEN);
    }
    m_nextGap = W + 40;  // erste Säule kommt von rechts herein
    spawn(W + 40);

    lv_obj_add_flag(m_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_hint, LV_OBJ_FLAG_HIDDEN);
    ui::setSwipeLocked(true);
    lv_timer_resume(m_timer);
    updateScore();
}

void RunnerApp::stop(bool crashed) {
    if (m_state != State::Running) return;
    lv_timer_pause(m_timer);
    ui::setSwipeLocked(false);

    bool record = m_points > m_best_;
    if (record) {
        m_best_ = m_points;
        settings::putU32(KEY_BEST, m_best_);
    }

    if (!crashed) {  // Kachel verlassen / Display aus -> still zurücksetzen
        m_state = State::Ready;
        showReady();
        return;
    }

    m_state  = State::Over;
    m_overAt = millis();
    haptics::failure();
    lv_label_set_text(m_title, record ? "Neuer Rekord!" : "Autsch!");
    lv_obj_align(m_title, LV_ALIGN_CENTER, 0, -46);
    lv_label_set_text_fmt(m_hint, "%lu Punkte  ·  Tippen", (unsigned long)m_points);
    lv_obj_align(m_hint, LV_ALIGN_CENTER, 0, -18);
    lv_obj_clear_flag(m_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_hint, LV_OBJ_FLAG_HIDDEN);
    updateScore();
}

// -----------------------------------------------------------------------------
//  Spielschleife
// -----------------------------------------------------------------------------
void RunnerApp::spawn(float x) {
    bool pair = m_points > 150 && rnd(0, 3) == 0;
    int count = pair ? 2 : 1;
    for (int n = 0; n < count; n++) {
        for (auto& o : m_obs) {
            if (o.active) continue;
            o.active = true;
            o.w = rnd(12, 20);
            o.h = pair ? rnd(18, 30) : rnd(20, 42);
            o.x = x;
            lv_obj_set_size(o.obj, o.w, o.h);
            lv_obj_set_pos(o.obj, lv_coord_t(o.x), GROUND_Y - o.h);
            lv_obj_clear_flag(o.obj, LV_OBJ_FLAG_HIDDEN);
            x += o.w + 4;
            break;
        }
    }
    // Abstand so wählen, dass nach der Landung noch Reaktionszeit bleibt
    float airtime = 2.0f * JUMP_V / GRAVITY;
    m_nextGap = m_speed * airtime + rnd(50, 170);
}

void RunnerApp::step() {
    if (display::isAsleep()) {  // Display aus -> Spiel nicht blind weiterlaufen lassen
        stop(false);
        return;
    }
    m_frame++;

    // Physik
    if (m_y > 0 || m_vy > 0) {
        m_y  += m_vy;
        m_vy -= GRAVITY;
        if (m_y <= 0) {
            m_y = 0;
            m_vy = 0;
            if (m_jumpBuffer) m_vy = JUMP_V;
        }
    }
    if (m_jumpBuffer) m_jumpBuffer--;

    m_speed = min(SPEED_MAX, SPEED_START + m_points * SPEED_GAIN);
    m_distance += m_speed;
    uint32_t pts = uint32_t(m_distance / PX_PER_POINT);
    if (pts != m_points) {
        if (pts / 100 != m_points / 100) haptics::tap();  // jede 100 Punkte
        m_points = pts;
        updateScore();
    }

    // Säulen bewegen + Kollision (Hitbox leicht verkleinert -> fühlt sich fair an)
    float rightmost = -1000;
    lv_coord_t pTop    = GROUND_Y - LEG_H - lv_coord_t(m_y) - PH + 4;
    lv_coord_t pBottom = GROUND_Y - lv_coord_t(m_y) - 2;
    for (auto& o : m_obs) {
        if (!o.active) continue;
        o.x -= m_speed;
        if (o.x + o.w < 0) {
            o.active = false;
            lv_obj_add_flag(o.obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_x(o.obj, lv_coord_t(o.x));
        rightmost = max(rightmost, o.x + o.w);

        bool overlapX = o.x < PLAYER_X + PW - 4 && o.x + o.w > PLAYER_X + 4;
        bool overlapY = pBottom > GROUND_Y - o.h && pTop < GROUND_Y;
        if (overlapX && overlapY) {
            layoutPlayer();
            stop(true);
            return;
        }
    }
    if (W - rightmost >= m_nextGap) spawn(W + 4);

    // Boden-Striche und Wolken
    for (int i = 0; i < DASHES; i++) {
        m_dashX[i] -= m_speed;
        if (m_dashX[i] < -16) m_dashX[i] += W + 32;
        lv_obj_set_x(m_dashes[i], lv_coord_t(m_dashX[i]));
    }
    if (m_frame % 3 == 0) {
        for (int i = 0; i < CLOUDS; i++) {
            m_cloudX[i] -= m_speed * 0.6f;
            if (m_cloudX[i] < -44) m_cloudX[i] += W + 88;
            lv_obj_set_x(m_clouds[i], lv_coord_t(m_cloudX[i]));
        }
    }

    layoutPlayer();
}

void RunnerApp::layoutPlayer() {
    lv_coord_t y = GROUND_Y - LEG_H - PH - lv_coord_t(m_y);
    lv_obj_set_pos(m_player, PLAYER_X, y);

    // Beine: am Boden abwechselnd, in der Luft beide angezogen
    bool airborne = m_y > 0.5f;
    bool phase    = (m_frame / 5) % 2;
    for (int i = 0; i < 2; i++) {
        lv_coord_t lift = airborne ? 3 : ((phase == (i == 0)) ? 3 : 0);
        lv_obj_set_pos(m_legs[i], PLAYER_X + 4 + i * 10, y + PH - 2 - lift);
    }
}

void RunnerApp::updateScore() {
    lv_label_set_text_fmt(m_score, "%lu", (unsigned long)m_points);
    lv_obj_align(m_score, LV_ALIGN_TOP_RIGHT, -18, 12);
    lv_label_set_text_fmt(m_best, ICON_TROPHY " %lu", (unsigned long)m_best_);
}
