#include "hop_tower_app.h"

#include <esp_random.h>
#include <math.h>

#include "core/settings.h"
#include "hal/display.h"
#include "hal/haptics.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/ui_core.h"

namespace {

constexpr const char* KEY_BEST  = "hop_hi";
constexpr const char* KEY_DROPS = "hop_drops";

constexpr uint32_t FRAME_MS = 20;            // 50 fps
constexpr lv_coord_t W = 240, H = 240;
constexpr float WALL   = 6;                  // Wanddicke
constexpr float INNER  = W - 2 * WALL;       // 228 px Schachtbreite
constexpr float CX     = W / 2.0f;

constexpr float SPACING = 84;                // Etagenabstand -> ~2,5 Etagen sichtbar
constexpr float T       = 8;                 // Plattformdicke
constexpr float J       = 24;                // Würfel
constexpr float CAM_OFF = 68;                // Würfel steht im unteren Drittel

constexpr float GRAVITY = 0.75f;
const float JUMP_V      = sqrtf(2 * GRAVITY * (SPACING + 10));  // knapp über die nächste Plattform

constexpr float STUCK_MIN = 0.30f;           // < 30 % der Würfelbreite -> abprallen
constexpr float SQUEEZE   = 11;              // so schmal wird er eingeklemmt
constexpr float PUSH_TAP  = 0.17f;           // ~6 Taps zum Rausdrücken
constexpr float PUSH_SLIP = 0.007f;          // rutscht ohne Tippen zurück

constexpr uint8_t BUFFER_FRAMES = 5;         // 100 ms

// Schwierigkeitskurve (zum Feintunen): ab Etage -> Schließzeit, Flüssigkeit
struct Level {
    int32_t fromFloor;
    float closeSec;   // Plattform schließt in ...
    float lavaMul;    // Flüssigkeit relativ zum Grundtempo
};
constexpr Level LEVELS[] = {
    {0, 1.2f, 1.0f},
    {10, 0.9f, 1.3f},
    {30, 0.7f, 1.6f},
    {60, 0.5f, 2.0f},  // Minimum
};
// Flüssigkeit: bei 1,0× durchquert sie eine Etage in 1,8 s -> nie mehr als eine Etage Vorsprung
constexpr float LAVA_BASE = SPACING / (1.8f * 1000.0f / FRAME_MS);   // ≈ 0,93 px/Frame
constexpr float LAVA_LAG  = SPACING - T;     // höchstens so weit unter dem Würfel (= Etage darunter)

const Level& levelFor(int32_t floorIndex) {
    const Level* l = &LEVELS[0];
    for (const auto& x : LEVELS) if (floorIndex >= x.fromFloor) l = &x;
    return *l;
}

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

void hide(lv_obj_t* o) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }
void show(lv_obj_t* o) { lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); }

}  // namespace

// =============================================================================
//  Aufbau
// =============================================================================
void HopTowerApp::create(lv_obj_t* tile) {
    m_tile = tile;
    m_best = settings::getU32(KEY_BEST, 0);
    m_dropsTotal = settings::getU32(KEY_DROPS, 0);

    lv_style_init(&m_platStyle);

    for (auto& f : m_floors) {
        f.rectL = rect(tile, 10, T, 3);
        f.rectR = rect(tile, 10, T, 3);
        lv_obj_add_style(f.rectL, &m_platStyle, 0);
        lv_obj_add_style(f.rectR, &m_platStyle, 0);
        f.drop = lv_label_create(tile);
        lv_obj_set_style_text_font(f.drop, &font_title, 0);
        lv_obj_set_style_text_color(f.drop, theme::tint(theme::Tint::Cyan), 0);
        lv_label_set_text(f.drop, ICON_DROP);
        hide(f.drop);
    }

    // Würfel: kräftiges Grün, zwei Augen
    m_jelly = rect(tile, J, J, 6);
    lv_obj_set_style_bg_color(m_jelly, theme::tint(theme::Tint::Green), 0);
    for (int i = 0; i < 2; i++) {
        m_eyes[i] = rect(m_jelly, 6, 7, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_color(m_eyes[i], lv_color_white(), 0);
        lv_obj_t* pupil = rect(m_eyes[i], 3, 3, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_color(pupil, lv_color_black(), 0);
        lv_obj_align(pupil, LV_ALIGN_BOTTOM_MID, 0, -1);
    }

    for (auto& p : m_part) {
        p = rect(tile, 5, 5, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_color(p, theme::tint(theme::Tint::Green), 0);
        hide(p);
    }

    // Flüssigkeit: Säulen mit Sinus-Oberkante (leuchtendes Pink)
    for (int i = 0; i < WAVE_COLS; i++) {
        m_wave[i] = rect(tile, W / WAVE_COLS + 1, 10, 0);
        lv_obj_set_style_bg_color(m_wave[i], theme::tint(theme::Tint::Pink), 0);
        lv_obj_set_style_bg_opa(m_wave[i], LV_OPA_90, 0);
    }

    // Wände (über der Flüssigkeit, damit der Schacht klar bleibt)
    for (int i = 0; i < 2; i++) {
        m_walls[i] = rect(tile, lv_coord_t(WALL), H, 0);
        lv_obj_set_pos(m_walls[i], i ? lv_coord_t(W - WALL) : 0, 0);
    }

    m_score = lv_label_create(tile);
    lv_obj_add_style(m_score, theme::textLarge(), 0);
    lv_label_set_text(m_score, "0");

    // Start-/Game-Over-Anzeige
    m_overlay = lv_obj_create(tile);
    lv_obj_remove_style_all(m_overlay);
    lv_obj_clear_flag(m_overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(m_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(m_overlay, 2, 0);
    m_ovTitle = theme::label(m_overlay, theme::textTitle(), "");
    m_ovBig   = lv_label_create(m_overlay);
    lv_obj_add_style(m_ovBig, theme::textLarge(), 0);
    m_ovLine1 = theme::label(m_overlay, theme::textCaption(), "");
    m_ovLine2 = theme::label(m_overlay, theme::textCaption(), "");
    m_ovHint  = theme::label(m_overlay, theme::textHeadline(), "");
    lv_obj_add_style(m_ovHint, theme::textAccent(), 0);
    lv_obj_set_style_pad_top(m_ovHint, 6, 0);

    applyColors();
    theme::onChange([this] { applyColors(); });

    // Springen auf PRESSED (sofort), Start/Neustart auf CLICKED (Wischen bleibt möglich)
    lv_obj_add_event_cb(
        tile,
        [](lv_event_t* e) {
            auto* self = static_cast<HopTowerApp*>(lv_event_get_user_data(e));
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED && self->m_state == State::Running) {
                self->tap();
            } else if (code == LV_EVENT_CLICKED) {
                if (self->m_state == State::Ready) self->start();
                else if (self->m_state == State::Over && millis() - self->m_overAt > 700) self->start();
            }
        },
        LV_EVENT_ALL, this);

    m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<HopTowerApp*>(t->user_data)->step(); },
                              FRAME_MS, this);
    lv_timer_pause(m_timer);
    showReady();
}

void HopTowerApp::applyColors() {
    const theme::Palette& p = theme::colors();
    lv_style_set_bg_color(&m_platStyle, p.textSecondary);
    lv_obj_report_style_change(&m_platStyle);
    for (auto* w : m_walls) lv_obj_set_style_bg_color(w, p.surfaceAlt, 0);
    lv_obj_set_style_bg_color(m_overlay, p.bg, 0);
}

void HopTowerApp::onShow() {
    if (m_state != State::Running) showReady();
    lv_timer_resume(m_timer);  // auch im Startbildschirm: Würfel wabbelt, Flüssigkeit wellt
}

void HopTowerApp::onHide() {
    stop(false);
    lv_timer_pause(m_timer);
}

bool HopTowerApp::onButton() {
    if (m_state != State::Running) return false;  // sonst: Display aus wie gewohnt
    tap();
    return true;
}

// =============================================================================
//  Etagen
// =============================================================================
void HopTowerApp::initFloor(Floor& f, int32_t i) {
    f.index = i;
    f.passed = f.fast = false;
    f.t = 0;
    f.delay = 0;
    f.hasDrop = i >= 3 && rnd(0, 9) == 0;
    f.dropTaken = false;
    if (i <= 0) {  // Boden
        f.st = Plat::Closed;
        f.open = 0;
        f.hasDrop = false;
    } else {
        f.st = Plat::Open;
        f.open = 1;
        // Mal schneller, mal langsamer: ±40 % um den Wert der Kurve
        f.dur = levelFor(i).closeSec * (1000.0f / FRAME_MS) * rnd(60, 140) / 100.0f;
    }
}

HopTowerApp::Floor& HopTowerApp::slotFor(int32_t i) {
    Floor& f = m_floors[((i % SLOTS) + SLOTS) % SLOTS];
    if (f.index != i) initFloor(f, i);
    return f;
}

void HopTowerApp::scheduleClose(int32_t i) {
    Floor& f = slotFor(i);
    if (f.st != Plat::Open) return;
    f.st = Plat::Delay;
    f.delay = uint16_t(rnd(6, 25));  // 0,1–0,5 s nach der Landung fahren sie los
}

void HopTowerApp::updateFloors() {
    int32_t base = int32_t(floorf(m_cam / SPACING)) - 1;
    for (int32_t i = base; i < base + SLOTS; i++) {
        if (i <= 0) continue;
        Floor& f = slotFor(i);
        if (f.st == Plat::Delay && --f.delay == 0) f.st = Plat::Closing;
        if (f.fast && f.st != Plat::Closed) f.st = Plat::Closing;
        if (f.st != Plat::Closing) continue;
        f.t += f.fast ? f.dur / 6 : 1;  // nach dem Durchspringen in ~6 Frames zu
        if (f.t >= f.dur) {
            f.t = f.dur;
            f.open = 0;
            f.st = Plat::Closed;
        } else {
            f.open = 1.0f - powf(f.t / f.dur, 1.3f);  // leicht beschleunigt
        }
    }
}

// Freie Breite in der Mitte (für Kollision und Zeichnen)
float HopTowerApp::gapOf(const Floor& f) const {
    // offen = Hälften stecken ganz in den Wänden
    return f.open <= 0 ? 0 : INNER * f.open;
}

// =============================================================================
//  Ablauf
// =============================================================================
void HopTowerApp::resetWorld() {
    for (auto& f : m_floors) f.index = -1;
    m_by = 0;
    m_vy = 0;
    m_stand = m_from = 0;
    m_mode = Mode::Standing;
    m_checked = false;
    m_stuckP = 0;
    m_cam = -CAM_OFF;
    m_lava = -60;
    m_trapped = false;
    m_buffer = m_dying = m_pop = 0;
    m_wob = m_wobV = 0;
    m_points = m_drops = 0;
    m_newRecord = false;
    lv_obj_set_style_bg_opa(m_jelly, LV_OPA_COVER, 0);
    for (auto* p : m_part) hide(p);
}

void HopTowerApp::setOverlay(bool visible, bool dim) {
    if (!visible) {
        hide(m_overlay);
        return;
    }
    show(m_overlay);
    if (dim) {  // Game Over: ganzer Bildschirm abgedunkelt
        lv_obj_set_size(m_overlay, W, H);
        lv_obj_set_pos(m_overlay, 0, 0);
        lv_obj_set_style_bg_opa(m_overlay, LV_OPA_80, 0);
    } else {    // Start: nur oberer Bereich, Würfel bleibt sichtbar
        lv_obj_set_size(m_overlay, W, 140);
        lv_obj_set_pos(m_overlay, 0, 6);
        lv_obj_set_style_bg_opa(m_overlay, LV_OPA_TRANSP, 0);
    }
}

void HopTowerApp::showReady() {
    m_state = State::Ready;
    resetWorld();
    for (int32_t i = 0; i < SLOTS - 1; i++) slotFor(i);

    lv_label_set_text(m_ovTitle, "Hüpfturm");
    hide(m_ovBig);
    lv_label_set_text_fmt(m_ovLine1, ICON_TROPHY " %lu     " ICON_DROP " %lu", (unsigned long)m_best,
                          (unsigned long)m_dropsTotal);
    hide(m_ovLine2);
    lv_label_set_text(m_ovHint, "Tippen zum Starten");
    setOverlay(true, false);
    hide(m_score);
    render();
}

void HopTowerApp::start() {
    if (m_state != State::Ready) showReady();
    m_state = State::Running;
    setOverlay(false, false);
    show(m_score);
    updateScore();
    scheduleClose(1);
    ui::setSwipeLocked(true);
    lv_timer_resume(m_timer);
}

void HopTowerApp::stop(bool crashed) {
    if (m_state != State::Running && m_state != State::Dying) return;
    ui::setSwipeLocked(false);

    if (m_points > m_best) {
        m_best = m_points;
        m_newRecord = true;
        settings::putU32(KEY_BEST, m_best);
    }
    if (m_drops) settings::putU32(KEY_DROPS, m_dropsTotal);

    if (!crashed) {  // Kachel verlassen / Display aus -> still zurücksetzen
        showReady();
        return;
    }
    m_state = State::Dying;  // erst zerlaufen lassen, dann Game-Over-Anzeige
    m_dying = 36;
    haptics::failure();
}

void HopTowerApp::showOver() {
    m_state = State::Over;
    m_overAt = millis();
    lv_label_set_text(m_ovTitle, "Game Over");
    show(m_ovBig);
    lv_label_set_text_fmt(m_ovBig, "%lu", (unsigned long)m_points);
    if (m_newRecord) {
        lv_label_set_text_fmt(m_ovLine1, ICON_TROPHY " %lu  ·  NEU!", (unsigned long)m_best);
    } else {
        lv_label_set_text_fmt(m_ovLine1, ICON_TROPHY " %lu", (unsigned long)m_best);
    }
    show(m_ovLine2);
    lv_label_set_text_fmt(m_ovLine2, ICON_DROP " +%lu  (gesamt %lu)", (unsigned long)m_drops,
                          (unsigned long)m_dropsTotal);
    lv_label_set_text(m_ovHint, "Tippen für Neustart");
    setOverlay(true, true);
    hide(m_score);
}

// =============================================================================
//  Eingabe & Physik
// =============================================================================
void HopTowerApp::tap() {
    switch (m_mode) {
        case Mode::Standing:
            jump();
            break;
        case Mode::Air:
            if (m_vy < 0) m_buffer = BUFFER_FRAMES;  // kurz vor der Landung -> danach springen
            break;
        case Mode::Stuck:
            m_stuckP += PUSH_TAP;
            m_wobV += 0.05f;  // sichtbares Drücken
            break;
    }
}

void HopTowerApp::jump() {
    m_mode = Mode::Air;
    m_from = m_stand;
    m_vy = JUMP_V;
    m_checked = false;
    m_buffer = 0;
    m_wob = -0.28f;  // Absprung: gestreckt
    m_wobV = 0;
}

void HopTowerApp::land(int32_t i, bool pop) {
    m_mode = Mode::Standing;
    m_stand = i;
    m_by = i * SPACING;
    m_vy = 0;
    m_wob = pop ? 0.42f : 0.32f;  // Landung: gestaucht, danach nachwabbeln
    m_wobV = 0;

    if (uint32_t(i) > m_points) {
        m_points = i;
        updateScore();
        if (m_points % 10 == 0) haptics::success();
        Floor& f = slotFor(i);
        if (f.hasDrop && !f.dropTaken) {
            f.dropTaken = true;
            m_drops++;
            m_dropsTotal++;
            haptics::tap();
        }
        scheduleClose(i + 1);
        m_trapped = false;
    }
    if (pop) {
        m_pop = 14;
        haptics::tap();
    }
    if (m_buffer) jump();
}

void HopTowerApp::physics() {
    switch (m_mode) {
        case Mode::Standing: {
            // Eingesperrt? (Plattform darüber zu) -> Flüssigkeit holt ihn in ~2 s
            if (!m_trapped && gapOf(slotFor(m_stand + 1)) < STUCK_MIN * J) {
                m_trapped = true;
                m_trapV = fmaxf(0.5f, (m_by - m_lava) / 100.0f);
            }
            break;
        }
        case Mode::Air: {
            float prev = m_by;
            m_vy -= GRAVITY;
            m_by += m_vy;
            int32_t next = m_from + 1;
            Floor& f = slotFor(next);
            float under = next * SPACING - T;

            if (m_vy > 0 && !m_checked && prev + J <= under && m_by + J > under) {
                m_checked = true;
                float gap = gapOf(f);
                if (gap >= J) {
                    f.passed = true;  // sauber durch
                } else if (gap >= STUCK_MIN * J) {
                    m_mode = Mode::Stuck;  // eingeklemmt
                    m_stuckP = 0;
                    m_vy = 0;
                    break;
                } else {
                    m_by = under - J;  // Kopfstoß -> abprallen
                    m_vy = -1.5f;
                    haptics::tap();
                }
            }
            if (f.passed && m_by >= next * SPACING) f.fast = true;  // hinter ihm zu

            if (m_vy <= 0) {
                int32_t m = int32_t(floorf(prev / SPACING));
                if (m >= 0 && prev >= m * SPACING && m_by < m * SPACING) land(m, false);
            }
            if (m_buffer) m_buffer--;
            break;
        }
        case Mode::Stuck: {
            m_stuckP = fmaxf(0.0f, m_stuckP - PUSH_SLIP);
            if (m_stuckP >= 1.0f) {
                Floor& f = slotFor(m_from + 1);
                f.passed = f.fast = true;
                land(m_from + 1, true);
            }
            break;
        }
    }
}

// =============================================================================
//  Spielschleife
// =============================================================================
void HopTowerApp::step() {
    if (display::isAsleep()) {
        if (m_state == State::Running || m_state == State::Dying) stop(false);
        lv_timer_pause(m_timer);
        return;
    }
    m_frame++;

    // Squash & Stretch: gedämpfte Feder
    m_wobV += -0.3f * m_wob - 0.2f * m_wobV;
    m_wob += m_wobV;

    if (m_state == State::Ready) {
        if (m_frame % 100 == 0) m_wobV += 0.12f;  // wabbelt im Startbildschirm
        render();
        return;
    }
    if (m_state == State::Over) {
        render();
        return;
    }
    if (m_state == State::Dying) {
        m_lava += 0.4f;
        if (--m_dying == 0) showOver();
        render();
        return;
    }

    updateFloors();
    physics();

    // Flüssigkeit: Tempo laut Kurve, Gummiband bei großem Vorsprung, schnell wenn eingesperrt
    float v = LAVA_BASE * levelFor(m_points).lavaMul;
    float lead = m_by - m_lava;
    if (lead > 2.2f * SPACING) v += (lead - 2.2f * SPACING) * 0.015f;
    if (m_trapped) v = fmaxf(v, m_trapV);
    m_lava += v;
    // max. eine Etage Vorsprung (gemessen an der Etage, auf der er steht – nicht am Sprung)
    float minLava = m_stand * SPACING - LAVA_LAG;
    if (m_lava < minLava) m_lava = minLava;
    if (m_lava < m_cam - 8) m_lava = m_cam - 8;              // immer knapp sichtbar

    // Kamera: nur nach oben, weich
    float target = m_stand * SPACING - CAM_OFF;
    if (target > m_cam) m_cam += (target - m_cam) * 0.1f;

    if (m_lava >= m_by + 2) {
        render();
        stop(true);
        return;
    }
    render();
}

// =============================================================================
//  Darstellung
// =============================================================================
void HopTowerApp::drawGap(Floor& f, lv_coord_t y, float gap) {
    // Zwei Hälften fahren aus den Wänden herein, Fuge exakt senkrecht in der Mitte
    lv_coord_t half = lv_coord_t((INNER - gap) / 2);
    if (half <= 0) {
        hide(f.rectL);
        hide(f.rectR);
        return;
    }
    show(f.rectL);
    show(f.rectR);
    lv_obj_set_size(f.rectL, half, lv_coord_t(T));
    lv_obj_set_size(f.rectR, half, lv_coord_t(T));
    lv_obj_set_pos(f.rectL, lv_coord_t(WALL), y);
    lv_obj_set_pos(f.rectR, lv_coord_t(W - WALL) - half, y);
}

void HopTowerApp::renderFloor(Floor& f) {
    lv_coord_t y = lv_coord_t(H - (f.index * SPACING - m_cam));
    if (f.index < 0 || y > H + 4 || y < -20) {
        hide(f.rectL); hide(f.rectR); hide(f.drop);
        return;
    }
    float gap = gapOf(f);
    int32_t next = m_from + 1;
    if (f.index == next && m_state == State::Running) {
        if (m_mode == Mode::Stuck) gap = fmaxf(gap, SQUEEZE);                  // presst gegen den Würfel
        else if (f.passed && m_by < next * SPACING) gap = fmaxf(gap, J + 2);   // gerade mittendrin
    }
    drawGap(f, y, gap);

    if (f.hasDrop && !f.dropTaken) {
        show(f.drop);
        lv_coord_t bob = lv_coord_t(2.5f * sinf((m_frame + f.index * 7) * 0.12f));
        lv_obj_set_pos(f.drop, lv_coord_t(CX) - 8, y - 38 + bob);
    } else {
        hide(f.drop);
    }
}

void HopTowerApp::render() {
    int32_t base = int32_t(floorf(m_cam / SPACING)) - 1;
    for (int32_t i = base; i < base + SLOTS; i++) {
        if (i < 0) {
            Floor& f = m_floors[((i % SLOTS) + SLOTS) % SLOTS];
            hide(f.rectL); hide(f.rectR); hide(f.drop);
            continue;
        }
        renderFloor(slotFor(i));
    }

    // Würfel
    float w = J * (1 + m_wob), h = J * (1 - m_wob);
    float by = m_by;
    if (m_mode == Mode::Stuck && m_state == State::Running) {
        // seitlich zusammengequetscht, schiebt sich mit jedem Tap höher
        float gap = fmaxf(gapOf(slotFor(m_from + 1)), SQUEEZE);
        w = fminf(J, gap);
        h = fminf(J * 1.7f, J * J / w) * (1 - 0.3f * m_wob);
        float under = (m_from + 1) * SPACING - T;
        float b0 = under - h * 0.55f, b1 = (m_from + 1) * SPACING;
        by = b0 + (b1 - b0) * m_stuckP;
        m_by = by;
    }
    if (m_state == State::Dying || m_state == State::Over) {  // zerläuft in der Flüssigkeit
        float k = m_state == State::Over ? 0 : m_dying / 36.0f;
        w = J * (1.9f - 0.9f * k);
        h = fmaxf(3.0f, J * k);
        by = fminf(m_by, m_lava - 2);
        lv_obj_set_style_bg_opa(m_jelly, lv_opa_t(255 * k), 0);
    }
    lv_coord_t jw = lv_coord_t(w), jh = lv_coord_t(h);
    lv_coord_t jy = lv_coord_t(H - (by - m_cam)) - jh;
    lv_obj_set_size(m_jelly, jw, jh);
    lv_obj_set_pos(m_jelly, lv_coord_t(CX) - jw / 2, jy);
    lv_coord_t look = m_vy > 2 ? -1 : (m_vy < -2 ? 1 : 0);
    lv_coord_t eyeY = jh * 3 / 10 + look;
    lv_obj_set_pos(m_eyes[0], jw / 2 - (jw > 16 ? 8 : 6), eyeY);
    lv_obj_set_pos(m_eyes[1], jw / 2 + (jw > 16 ? 2 : 0), eyeY);

    // Plopp: kleine Kreise fliegen auseinander
    if (m_pop) {
        m_pop--;
        float r = (14 - m_pop) * 2.2f;
        for (int i = 0; i < PARTICLES; i++) {
            float a = i * (2 * float(M_PI) / PARTICLES) - float(M_PI) / 2;
            show(m_part[i]);
            lv_obj_set_pos(m_part[i], lv_coord_t(CX + cosf(a) * r) - 2, lv_coord_t(jy + jh / 2 + sinf(a) * r * 0.7f) - 2);
            lv_obj_set_style_bg_opa(m_part[i], lv_opa_t(m_pop * 18), 0);
        }
        if (!m_pop) for (auto* p : m_part) hide(p);
    }

    // Flüssigkeit mit welliger Oberfläche
    lv_coord_t ly = lv_coord_t(H - (m_lava - m_cam));
    float phase = m_frame * 0.15f;
    lv_coord_t colW = W / WAVE_COLS;
    for (int i = 0; i < WAVE_COLS; i++) {
        lv_coord_t top = ly + lv_coord_t(2.5f * sinf(phase + i * 0.55f));
        if (top >= H) {
            hide(m_wave[i]);
            continue;
        }
        show(m_wave[i]);
        lv_obj_set_pos(m_wave[i], i * colW, top);
        lv_obj_set_height(m_wave[i], H - top);
    }

}

void HopTowerApp::updateScore() {
    lv_label_set_text_fmt(m_score, "%lu", (unsigned long)m_points);
    lv_obj_align(m_score, LV_ALIGN_TOP_MID, 0, 8);
}
