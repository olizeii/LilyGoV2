#include "stopwatch_app.h"

#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {
lv_color_t orange() { return theme::tint(theme::Tint::Orange); }
lv_color_t green()  { return theme::tint(theme::Tint::Green); }
}  // namespace

uint32_t StopwatchApp::elapsedMs() const { return m_accMs + (m_running ? millis() - m_startMs : 0); }

void StopwatchApp::create(lv_obj_t* tile) {
    const theme::Palette& p = theme::colors();

    // Ring: eine Umdrehung pro Minute (wie der Sekundenzeiger)
    m_ring = lv_arc_create(tile);
    lv_obj_set_size(m_ring, 212, 212);
    lv_obj_center(m_ring);
    lv_arc_set_rotation(m_ring, 270);
    lv_arc_set_bg_angles(m_ring, 0, 360);
    lv_arc_set_range(m_ring, 0, 600);
    lv_arc_set_value(m_ring, 0);
    lv_obj_remove_style(m_ring, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(m_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(m_ring, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(m_ring, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(m_ring, p.surface, LV_PART_MAIN);
    lv_obj_set_style_arc_color(m_ring, orange(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(m_ring, true, LV_PART_INDICATOR);

    // Großer runder Knopf in der Mitte – die ganze Fläche ist Start/Stopp
    m_knob = lv_obj_create(tile);
    lv_obj_remove_style_all(m_knob);
    lv_obj_set_size(m_knob, 176, 176);
    lv_obj_center(m_knob);
    lv_obj_set_style_radius(m_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_knob, p.surface, 0);
    lv_obj_set_style_bg_opa(m_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m_knob, p.surfaceAlt, LV_STATE_PRESSED);
    lv_obj_clear_flag(m_knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m_knob, [](lv_event_t* e) {
        static_cast<StopwatchApp*>(lv_event_get_user_data(e))->toggle();
    }, LV_EVENT_CLICKED, this);

    m_time = theme::label(m_knob, theme::textLarge(), "0:00,00");
    lv_obj_align(m_time, LV_ALIGN_CENTER, 0, -6);

    m_hint = theme::label(m_knob, theme::textCaption(), "Tippen zum Starten");
    lv_obj_align(m_hint, LV_ALIGN_CENTER, 0, 30);

    // Zurücksetzen (nur gestoppt und > 0 sichtbar)
    m_reset = ui::roundButton(tile, LV_SYMBOL_REFRESH, 40, p.surfaceAlt, p.text, &font_caption);
    lv_obj_align(m_reset, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_add_event_cb(m_reset, [](lv_event_t* e) {
        static_cast<StopwatchApp*>(lv_event_get_user_data(e))->reset();
    }, LV_EVENT_CLICKED, this);

    theme::onChange([this] {
        const theme::Palette& c = theme::colors();
        lv_obj_set_style_arc_color(m_ring, c.surface, LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_knob, c.surface, 0);
        lv_obj_set_style_bg_color(m_knob, c.surfaceAlt, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(m_reset, c.surfaceAlt, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(m_reset, 0), c.text, 0);
    });

    // 30 ms nur solange sichtbar und laufend (sonst pausiert -> kein Strom)
    m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<StopwatchApp*>(t->user_data)->render(); }, 30, this);
    lv_timer_pause(m_timer);
    render();
}

void StopwatchApp::onShow() {
    render();
    if (m_running) lv_timer_resume(m_timer);
}

void StopwatchApp::onHide() { lv_timer_pause(m_timer); }

void StopwatchApp::toggle() {
    if (m_running) {
        m_accMs += millis() - m_startMs;
        m_running = false;
        lv_timer_pause(m_timer);
    } else {
        m_startMs = millis();
        m_running = true;
        lv_timer_resume(m_timer);
    }
    haptics::tap();
    render();
}

void StopwatchApp::reset() {
    if (m_running) return;
    m_accMs = 0;
    haptics::tap();
    render();
}

void StopwatchApp::render() {
    uint32_t ms = elapsedMs();
    uint32_t cs = (ms / 10) % 100;
    uint32_t s  = (ms / 1000) % 60;
    uint32_t m  = (ms / 60000) % 60;
    uint32_t h  = ms / 3600000;
    char buf[20];
    if (h) snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else   snprintf(buf, sizeof(buf), "%lu:%02lu,%02lu", (unsigned long)m, (unsigned long)s, (unsigned long)cs);
    ui::setText(m_time, buf);

    lv_arc_set_value(m_ring, (ms / 100) % 600);
    lv_obj_set_style_arc_color(m_ring, m_running ? orange() : green(), LV_PART_INDICATOR);

    ui::setText(m_hint, m_running ? "Tippen zum Stoppen" : (ms ? "Tippen zum Fortsetzen" : "Tippen zum Starten"));
    if (!m_running && ms) lv_obj_clear_flag(m_reset, LV_OBJ_FLAG_HIDDEN);
    else                  lv_obj_add_flag(m_reset, LV_OBJ_FLAG_HIDDEN);
}
