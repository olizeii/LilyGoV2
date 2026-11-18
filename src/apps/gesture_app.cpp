#include "gesture_app.h"

#include <math.h>

#include "core/settings.h"
#include "core/sfx.h"
#include "hal/haptics.h"
#include "hal/motion.h"
#include "ui/theme.h"
#include "ui/widgets.h"

using theme::Tint;

namespace {

constexpr float WHIP_G   = 3.0f;    // Mindest-Ruck (ohne Schwerkraft) für die Peitsche
constexpr float GUN_G    = 2.2f;    // fürs Pistolen-Wippen reicht weniger
constexpr uint32_t PAUSE = 450;     // ms bis zur nächsten Auslösung
constexpr float SHAKE_ON  = 0.9f;   // Dauerfeuer: ab dieser Wackel-Stärke (g) wird geschossen ...
constexpr float SHAKE_OFF = 0.5f;   // ... und darunter wieder Ruhe (Hysterese)
constexpr uint32_t BURST_MS = 85;   // ~12 Schuss pro Sekunde

const char* const NAMES[]  = {"Peitsche", "Pistole", "Auto", "Dauerfeuer"};
const char* const EMOJIS[] = {"\xF0\x9F\xA4\xA0", "\xF0\x9F\x92\xA5", "\xE2\x9C\xA8", "\xF0\x9F\x94\xA5"};   // 🤠 💥 ✨ 🔥

}  // namespace

void GestureApp::create(lv_obj_t* tile) {
    const theme::Palette& p = theme::colors();
    m_mode = settings::getU8("gestMode", Auto) % MODES;

    lv_obj_t* title = theme::label(tile, theme::textHeadline(), "Sound-Gesten");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Großer Kreis: blinkt beim Auslösen, antippen = Probe
    m_circle = lv_obj_create(tile);
    lv_obj_remove_style_all(m_circle);
    lv_obj_set_size(m_circle, 96, 96);
    lv_obj_align(m_circle, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_radius(m_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(m_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m_circle, p.surface, 0);
    lv_obj_set_style_bg_color(m_circle, p.surfaceAlt, LV_STATE_PRESSED);
    lv_obj_clear_flag(m_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m_circle, [](lv_event_t* e) {
        auto* a = static_cast<GestureApp*>(lv_event_get_user_data(e));
        a->fire(a->m_mode == Gun || a->m_mode == Burst);
    }, LV_EVENT_CLICKED, this);

    m_emoji = theme::label(m_circle, theme::textTitle(), "");
    lv_obj_align(m_emoji, LV_ALIGN_CENTER, 0, -10);
    m_label = theme::label(m_circle, theme::textCaption(), "");
    lv_obj_align(m_label, LV_ALIGN_CENTER, 0, 18);

    m_hint = theme::label(tile, theme::textCaption(), "");
    lv_obj_set_style_text_color(m_hint, p.textSecondary, 0);
    lv_obj_align(m_hint, LV_ALIGN_TOP_MID, 0, 138);

    // Modus-Auswahl: 2×2 Pillen
    lv_obj_t* row = ui::box(tile);
    lv_obj_set_size(row, 228, 74);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    for (uint8_t i = 0; i < MODES; i++) {
        lv_obj_t* b = lv_obj_create(row);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 108, 34);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(b, reinterpret_cast<void*>((intptr_t)i));
        lv_obj_t* l = theme::label(b, theme::textCaption(), NAMES[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            auto* a = static_cast<GestureApp*>(lv_event_get_user_data(e));
            a->setMode((uint8_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e)));
        }, LV_EVENT_CLICKED, this);
        m_seg[i] = b;
    }

    theme::onChange([this] { refresh(); });

    m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<GestureApp*>(t->user_data)->sample(); }, 10, this);
    lv_timer_pause(m_timer);
    refresh();
}

void GestureApp::onShow() {
    motion::setGestureMode(true);
    m_primed = false;
    if (!sfx::open()) ui::setText(m_hint, "Lautsprecher belegt");
    lv_timer_resume(m_timer);
    refresh();
}

void GestureApp::onHide() {
    lv_timer_pause(m_timer);
    sfx::close();
    motion::setGestureMode(false);
}

void GestureApp::setMode(uint8_t m) {
    m_mode = m % MODES;
    m_shaking = false;
    m_energy = 0;
    settings::putU8("gestMode", m_mode);
    haptics::tap();
    refresh();
}

void GestureApp::fire(bool gun) {
    m_lastFire = millis();
    m_flashUntil = m_lastFire + 180;
    m_count++;
    sfx::play(gun ? sfx::Sound::Gun : sfx::Sound::Whip);
    haptics::tap();
    lv_obj_set_style_bg_color(m_circle, theme::tint(gun ? Tint::Orange : Tint::Red), 0);
    ui::setText(m_emoji, gun ? EMOJIS[Gun] : EMOJIS[Whip]);
}

void GestureApp::sample() {
    if (m_flashUntil && millis() > m_flashUntil) {
        m_flashUntil = 0;
        refresh();
    }

    float a[3];
    if (!motion::readAccel(a[0], a[1], a[2])) return;

    // Schwerkraft herausfiltern (langsamer Tiefpass), übrig bleibt die schnelle Bewegung
    if (!m_primed) {
        for (int i = 0; i < 3; i++) m_g[i] = a[i];
        m_primed = true;
        return;
    }
    float d[3], mag2 = 0;
    for (int i = 0; i < 3; i++) {
        m_g[i] += 0.04f * (a[i] - m_g[i]);
        d[i] = a[i] - m_g[i];
        mag2 += d[i] * d[i];
    }
    float mag = sqrtf(mag2);

    // Dauerfeuer: solange kräftig gewackelt wird, Schuss auf Schuss; still halten = Ruhe
    if (m_mode == Burst) {
        m_energy += 0.12f * (mag - m_energy);   // ~80 ms Glättung
        if (!m_shaking && m_energy > SHAKE_ON) m_shaking = true;
        if (m_shaking && m_energy < SHAKE_OFF) m_shaking = false;
        if (m_shaking && millis() - m_lastFire >= BURST_MS) fire(true);
        return;
    }
    if (millis() - m_lastFire < PAUSE) return;

    switch (m_mode) {
        case Whip:
            if (mag > WHIP_G) fire(false);
            break;
        case Gun:
            if (mag > GUN_G) fire(true);
            break;
        default: {
            // Auto: Pistole = Hand wippt nach oben -> Bewegung vor allem senkrecht zum Display (z),
            // Peitsche = Schlag in der Ebene des Arms (x/y) und meist heftiger
            if (mag < GUN_G) break;
            bool gun = fabsf(d[2]) > 0.65f * mag;
            if (gun || mag > WHIP_G) fire(gun);
            break;
        }
    }
}

void GestureApp::refresh() {
    const theme::Palette& p = theme::colors();
    if (!m_flashUntil) {
        lv_obj_set_style_bg_color(m_circle, p.surface, 0);
        ui::setText(m_emoji, EMOJIS[m_mode]);
    }
    ui::setText(m_label, NAMES[m_mode]);
    if (sfx::isOpen()) {
        ui::setText(m_hint, m_mode == Whip  ? "Hand wie eine Peitsche schlagen"
                          : m_mode == Gun   ? "Hand schnell nach oben wippen"
                          : m_mode == Burst ? "Schnell wackeln = Feuer, still = Ruhe"
                                            : "Peitschen- oder Schuss-Bewegung");
    }
    for (uint8_t i = 0; i < MODES; i++) {
        bool sel = i == m_mode;
        lv_obj_set_style_bg_color(m_seg[i], sel ? p.text : p.surface, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(m_seg[i], 0), sel ? p.bg : p.text, 0);
    }
}
