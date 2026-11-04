#include "hop_tower_app.h"

#include <esp_random.h>

#include "core/settings.h"
#include "hal/display.h"
#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/ui_core.h"

namespace {

constexpr const char* KEY_BEST = "hop_hi";

constexpr uint32_t FRAME_MS = 20;      // 50 fps
constexpr lv_coord_t W      = 240;
constexpr lv_coord_t H      = 240;

constexpr float SPACING     = 64;      // Abstand der Stockwerke
constexpr float BAR_H       = 8;       // Balkendicke
constexpr float D           = 22;      // Tropfen-Durchmesser
constexpr float PX          = (W - D) / 2;   // Tropfen links (fix in der Mitte)
constexpr float HIT_INSET   = 3;       // Hitbox etwas kleiner -> fühlt sich fair an

constexpr float GRAVITY     = 0.6f;
constexpr float JUMP_V      = 10.0f;   // Scheitel ~83 px: sicher durch ein Stockwerk, nie durch zwei
constexpr float CAM_OFFSET  = 56;      // Tropfen steht so weit über dem unteren Rand

constexpr float GAP_START   = 74;      // Lückenbreite am Anfang ...
constexpr float GAP_MIN     = 40;      // ... und ganz oben
constexpr float GAP_V_START = 1.1f;
constexpr float GAP_V_MAX   = 3.0f;
constexpr float LAVA_START  = 0.22f;   // px/Frame
constexpr float LAVA_MAX    = 0.85f;

// Stockwerksfarben (Apple-Systemfarben im Kreis)
constexpr theme::Tint FLOOR_TINTS[] = {
    theme::Tint::Blue, theme::Tint::Purple, theme::Tint::Pink,
    theme::Tint::Orange, theme::Tint::Yellow, theme::Tint::Green,
};

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
void HopTowerApp::create(lv_obj_t* tile) {
    m_tile  = tile;
    m_best_ = settings::getU32(KEY_BEST, 0);

    for (auto& f : m_floors) {
        f.left  = rect(tile, 10, BAR_H, 4);
        f.right = rect(tile, 10, BAR_H, 4);
    }

    // Tropfen: türkis, zwei Augen mit Pupillen
    m_player = rect(tile, D, D, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_color(m_player, theme::tint(theme::Tint::Teal), 0);
    for (int i = 0; i < 2; i++) {
        m_eyes[i] = rect(m_player, 7, 7, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_color(m_eyes[i], lv_color_white(), 0);
        lv_obj_t* pupil = rect(m_eyes[i], 3, 3, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_color(pupil, lv_color_black(), 0);
        lv_obj_align(pupil, LV_ALIGN_TOP_MID, 0, 1);
    }

    // Lava: Fläche + hellere Oberkante
    m_lava = rect(tile, W, H, 0);
    lv_obj_set_style_bg_color(m_lava, theme::tint(theme::Tint::Red), 0);
    lv_obj_set_style_bg_opa(m_lava, LV_OPA_80, 0);
    m_lavaTop = rect(tile, W, 4, 2);
    lv_obj_set_style_bg_color(m_lavaTop, theme::tint(theme::Tint::Orange), 0);

    // Anzeigen (über allem)
    m_best = theme::label(tile, theme::textCaption(), "");
    m_score = theme::label(tile, theme::textHeadline(), "0");
    m_title = theme::label(tile, theme::textTitle(), "");
    m_hint  = theme::label(tile, theme::textCaption(), "");

    applyColors();
    theme::onChange([this] { applyColors(); });

    // Springen auf PRESSED (sofort), Start/Neustart auf CLICKED (Wischen bleibt möglich)
    lv_obj_add_event_cb(
        tile,
        [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) return;
            static_cast<HopTowerApp*>(lv_event_get_user_data(e))->onPress(code == LV_EVENT_CLICKED);
        },
        LV_EVENT_ALL, this);

    m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<HopTowerApp*>(t->user_data)->step(); },
                              FRAME_MS, this);
    lv_timer_pause(m_timer);
    showReady();
}

void HopTowerApp::applyColors() {
    // Balken tragen Systemfarben -> funktionieren in Hell und Dunkel; nur Texte folgen dem Theme
    for (auto& f : m_floors) {
        if (f.index < 0) continue;
        lv_color_t c = f.index == 0 ? theme::colors().surfaceAlt
                                    : theme::tint(FLOOR_TINTS[f.index % (sizeof(FLOOR_TINTS) / sizeof(FLOOR_TINTS[0]))]);
        lv_obj_set_style_bg_color(f.left, c, 0);
        lv_obj_set_style_bg_color(f.right, c, 0);
    }
}

// -----------------------------------------------------------------------------
//  Welt
// -----------------------------------------------------------------------------
void HopTowerApp::initFloor(Floor& f, int32_t i) {
    f.index = i;
    f.closed = false;
    if (i == 0) {  // Boden ohne Lücke
        f.gapW = 0;
        f.gapX = 0;
        f.gapV = 0;
    } else {
        float t = min(1.0f, i / 60.0f);  // Schwierigkeit steigt bis Stockwerk 60
        f.gapW = GAP_START - (GAP_START - GAP_MIN) * t + rnd(-4, 4);
        float speed = GAP_V_START + (GAP_V_MAX - GAP_V_START) * t;
        speed *= rnd(85, 115) / 100.0f;
        f.gapV = rnd(0, 1) ? speed : -speed;
        f.gapX = rnd(0, int(W - f.gapW));
    }
    lv_color_t c = i == 0 ? theme::colors().surfaceAlt
                          : theme::tint(FLOOR_TINTS[i % (sizeof(FLOOR_TINTS) / sizeof(FLOOR_TINTS[0]))]);
    lv_obj_set_style_bg_color(f.left, c, 0);
    lv_obj_set_style_bg_color(f.right, c, 0);
}

HopTowerApp::Floor& HopTowerApp::slotFor(int32_t i) {
    Floor& f = m_floors[((i % SLOTS) + SLOTS) % SLOTS];
    if (f.index != i) initFloor(f, i);
    return f;
}

bool HopTowerApp::insideGap(const Floor& f) const {
    if (f.gapW <= 0) return false;
    return PX + HIT_INSET >= f.gapX && PX + D - HIT_INSET <= f.gapX + f.gapW;
}

void HopTowerApp::resetWorld() {
    for (auto& f : m_floors) f.index = -1;
    m_py = 0;
    m_vy = 0;
    m_stand = 0;
    m_cam = -CAM_OFFSET;
    m_lavaH = -70;
    m_squash = 0;
    m_jumpBuffer = 0;
    m_frame = 0;
    for (int32_t i = 0; i < SLOTS; i++) slotFor(i);
    // Erste Lücke nicht direkt über dem Tropfen starten lassen
    Floor& first = slotFor(1);
    first.gapX = first.gapV > 0 ? 0 : W - first.gapW;
}

// -----------------------------------------------------------------------------
//  Zustände
// -----------------------------------------------------------------------------
void HopTowerApp::showReady() {
    if (m_state == State::Running) return;
    m_state = State::Ready;
    resetWorld();
    m_points = 0;
    render();

    lv_label_set_text(m_title, "Hüpfturm");
    lv_obj_align(m_title, LV_ALIGN_CENTER, 0, -50);
    lv_label_set_text(m_hint, "Durch die Lücken springen");
    lv_obj_align(m_hint, LV_ALIGN_CENTER, 0, -22);
    lv_obj_clear_flag(m_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_hint, LV_OBJ_FLAG_HIDDEN);
    updateScore();
}

void HopTowerApp::onPress(bool clicked) {
    switch (m_state) {
        case State::Ready:
            if (clicked) start();
            break;
        case State::Running:
            if (clicked) break;
            if (m_stand >= 0) {
                m_vy = JUMP_V;
                m_stand = -1;
            } else {
                m_jumpBuffer = 8;  // kurz vor der Landung getippt -> direkt weiter
            }
            break;
        case State::Over:
            if (clicked && millis() - m_overAt > 600) {
                m_state = State::Ready;
                showReady();
                start();
            }
            break;
    }
}

void HopTowerApp::start() {
    m_state = State::Running;
    lv_obj_add_flag(m_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_hint, LV_OBJ_FLAG_HIDDEN);
    ui::setSwipeLocked(true);
    lv_timer_resume(m_timer);
}

void HopTowerApp::stop(bool crashed) {
    if (m_state != State::Running) return;
    lv_timer_pause(m_timer);
    ui::setSwipeLocked(false);

    bool record = m_points > m_best_;
    if (record) {
        m_best_ = m_points;
        settings::putU32(KEY_BEST, m_best_);
    }
    if (!crashed) {
        m_state = State::Ready;
        showReady();
        return;
    }

    m_state  = State::Over;
    m_overAt = millis();
    haptics::failure();
    lv_label_set_text(m_title, record ? "Neuer Rekord!" : "Zu heiß!");
    lv_obj_align(m_title, LV_ALIGN_CENTER, 0, -50);
    lv_label_set_text_fmt(m_hint, "Stockwerk %lu  ·  Tippen", (unsigned long)m_points);
    lv_obj_align(m_hint, LV_ALIGN_CENTER, 0, -22);
    lv_obj_clear_flag(m_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_hint, LV_OBJ_FLAG_HIDDEN);
    updateScore();
}

// -----------------------------------------------------------------------------
//  Spielschleife
// -----------------------------------------------------------------------------
void HopTowerApp::step() {
    if (display::isAsleep()) {
        stop(false);
        return;
    }
    m_frame++;

    // Lücken wandern (an den Rändern abprallen)
    int32_t base = int32_t(floorf(m_cam / SPACING));
    for (int32_t i = base; i < base + SLOTS; i++) {
        if (i < 0) continue;
        Floor& f = slotFor(i);
        if (f.gapW <= 0) continue;
        if (f.closed) {  // Hälften fahren zusammen
            float s = min(f.gapW, 6.0f);
            f.gapX += s / 2;
            f.gapW -= s;
            continue;
        }
        f.gapX += f.gapV;
        if (f.gapX < 0) { f.gapX = 0; f.gapV = -f.gapV; }
        if (f.gapX > W - f.gapW) { f.gapX = W - f.gapW; f.gapV = -f.gapV; }
    }

    // Physik
    if (m_stand < 0) {
        float prev = m_py;
        m_vy -= GRAVITY;
        m_py += m_vy;

        if (m_vy > 0) {
            // Nach oben: Kopf trifft die Unterkante des nächsten Balkens?
            int32_t j = int32_t(floorf((prev + D) / SPACING)) + 1;  // nächster Balken über dem Kopf
            float bottom = j * SPACING - BAR_H;
            if (prev + D <= bottom && m_py + D > bottom) {
                if (!insideGap(slotFor(j))) {
                    m_py = bottom - D;  // Kopfstoß: abprallen
                    m_vy = -1.0f;
                    haptics::tap();
                }
            }
        } else {
            // Nach unten: auf dem Balken darunter landen (der ist zu – wir kamen ja durch)
            int32_t j = int32_t(floorf(prev / SPACING));
            float top = j * SPACING;
            if (j >= 0 && prev >= top && m_py < top) {
                m_py = top;
                m_vy = 0;
                m_stand = j;
                m_squash = 8;
                if (uint32_t(j) > m_points) {
                    m_points = j;
                    if (m_points % 10 == 0) haptics::success();
                    updateScore();
                }
                if (m_jumpBuffer) {
                    m_vy = JUMP_V;
                    m_stand = -1;
                    m_jumpBuffer = 0;
                }
            }
        }
    }
    // Komplett über einem Balken -> der schließt sich hinter dem Tropfen
    if (m_stand < 0) {
        int32_t j = int32_t(floorf(m_py / SPACING));
        if (j >= 1) {
            Floor& f = slotFor(j);
            if (!f.closed && f.gapW > 0) f.closed = true;
        }
    }
    if (m_jumpBuffer) m_jumpBuffer--;
    if (m_squash) m_squash--;

    // Kamera folgt weich (nach oben schneller als nach unten)
    float target = (m_stand >= 0 ? m_stand * SPACING : m_py) - CAM_OFFSET;
    float k = target > m_cam ? 0.12f : 0.06f;
    m_cam += (target - m_cam) * k;

    // Lava steigt – nie mehr als knapp unter dem Bild, damit Druck bleibt
    float speed = min(LAVA_MAX, LAVA_START + m_points * 0.012f);
    m_lavaH += speed;
    if (m_lavaH < m_cam - 30) m_lavaH = m_cam - 30;

    if (m_lavaH >= m_py + 4) {
        render();
        stop(true);
        return;
    }
    render();
}

void HopTowerApp::render() {
    auto screenY = [this](float worldH) { return lv_coord_t(H - (worldH - m_cam)); };

    int32_t base = int32_t(floorf(m_cam / SPACING));
    for (int32_t i = base; i < base + SLOTS; i++) {
        if (i < 0) {  // unter dem Boden: Slot verstecken
            Floor& f = m_floors[((i % SLOTS) + SLOTS) % SLOTS];
            lv_obj_add_flag(f.left, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(f.right, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        Floor& f = slotFor(i);
        lv_coord_t y = screenY(i * SPACING);
        lv_obj_clear_flag(f.left, LV_OBJ_FLAG_HIDDEN);
        if (f.gapW <= 0) {
            lv_obj_set_size(f.left, W, BAR_H);
            lv_obj_set_pos(f.left, 0, y);
            lv_obj_add_flag(f.right, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_coord_t gx = lv_coord_t(f.gapX);
        lv_coord_t gw = lv_coord_t(f.gapW);
        if (gx > 0) {
            lv_obj_set_size(f.left, gx, BAR_H);
            lv_obj_set_pos(f.left, 0, y);
        } else {
            lv_obj_add_flag(f.left, LV_OBJ_FLAG_HIDDEN);
        }
        if (gx + gw < W) {
            lv_obj_clear_flag(f.right, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(f.right, W - gx - gw, BAR_H);
            lv_obj_set_pos(f.right, gx + gw, y);
        } else {
            lv_obj_add_flag(f.right, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Tropfen: beim Landen kurz breit gedrückt, im Sprung leicht gestreckt
    lv_coord_t w = lv_coord_t(D), h = lv_coord_t(D);
    if (m_squash) {
        lv_coord_t s = m_squash > 4 ? 8 - m_squash : m_squash;  // 0..4..0
        w += s;
        h -= s;
    } else if (m_stand < 0 && m_vy > 4) {
        w -= 2;
        h += 3;
    }
    lv_obj_set_size(m_player, w, h);
    lv_obj_set_pos(m_player, lv_coord_t(PX) - (w - lv_coord_t(D)) / 2, screenY(m_py) - h);
    // Augen schauen in Bewegungsrichtung
    lv_coord_t look = m_vy > 1 ? -1 : (m_vy < -1 ? 2 : 0);
    lv_obj_set_pos(m_eyes[0], w / 2 - 8, h / 2 - 6 + look);
    lv_obj_set_pos(m_eyes[1], w / 2 + 1, h / 2 - 6 + look);

    // Lava
    lv_coord_t ly = screenY(m_lavaH);
    if (ly >= H) {
        lv_obj_add_flag(m_lava, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_lavaTop, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(m_lava, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_lavaTop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(m_lava, 0, ly);
        lv_obj_set_height(m_lava, H - ly > 0 ? H - ly : 1);
        // leichte Welle an der Oberkante
        lv_obj_set_pos(m_lavaTop, lv_coord_t((m_frame / 3) % 8) - 4, ly - 2);
    }
}

void HopTowerApp::updateScore() {
    lv_label_set_text_fmt(m_score, "%lu", (unsigned long)m_points);
    lv_obj_align(m_score, LV_ALIGN_TOP_RIGHT, -18, 12);
    lv_label_set_text_fmt(m_best, ICON_TROPHY " %lu", (unsigned long)m_best_);
    lv_obj_align(m_best, LV_ALIGN_TOP_LEFT, 18, 14);
}
