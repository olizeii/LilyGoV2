#include "studio_app.h"

#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/ui_core.h"
#include "ui/widgets.h"

using theme::Tint;

namespace {

constexpr lv_coord_t AREA_X = 10;
constexpr lv_coord_t AREA_Y = 32;
constexpr lv_coord_t AREA_W = 220;
constexpr lv_coord_t LANE_H = 26;
constexpr lv_coord_t LANE_GAP = 6;
constexpr uint32_t SNAP = studio::RATE / 20;   // 50 ms Raster beim Verschieben

lv_coord_t laneY(int lane) { return lane * (LANE_H + LANE_GAP); }   // 0 = Beat

String fmtTime(uint32_t samples) {
    uint32_t s = samples / studio::RATE;
    char b[12];
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    return b;
}

lv_obj_t* bar(lv_obj_t* parent, lv_color_t c) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_radius(o, 7, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* barLabel(lv_obj_t* b, const char* t) {
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &font_caption, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, t);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    return l;
}

}  // namespace

// --- Skala ---------------------------------------------------------------------
uint32_t StudioApp::viewLength() const {
    if (m_view) return m_view;
    uint32_t len = studio::songLength() + studio::RATE;       // 1 s Luft zum Verschieben
    return max<uint32_t>(len, 10 * studio::RATE);
}

lv_coord_t StudioApp::xFor(uint32_t s) const { return (lv_coord_t)((uint64_t)s * AREA_W / viewLength()); }
uint32_t StudioApp::samplesFor(lv_coord_t dx) const { return (uint32_t)((uint64_t)max<lv_coord_t>(dx, 0) * viewLength() / AREA_W); }

// --- Aufbau --------------------------------------------------------------------
void StudioApp::create(lv_obj_t* tile) {
    const theme::Palette& p = theme::colors();

    m_header = theme::label(tile, theme::textCaption(), "");
    lv_obj_set_width(m_header, 150);
    lv_label_set_long_mode(m_header, LV_LABEL_LONG_DOT);
    lv_obj_align(m_header, LV_ALIGN_TOP_LEFT, AREA_X + 2, 8);

    m_timeLbl = theme::label(tile, theme::textCaption(), "0:00");
    lv_obj_set_style_text_color(m_timeLbl, p.textSecondary, 0);
    lv_obj_align(m_timeLbl, LV_ALIGN_TOP_RIGHT, -AREA_X - 2, 8);

    m_area = ui::box(tile);
    lv_obj_set_size(m_area, AREA_W, laneY(studio::MAX_CLIPS + 1) - LANE_GAP);
    lv_obj_set_pos(m_area, AREA_X, AREA_Y);

    // Spur-Hintergründe: antippen = Startpunkt setzen
    for (int lane = 0; lane <= studio::MAX_CLIPS; lane++) {
        lv_obj_t* bg = bar(m_area, p.surface);
        lv_obj_set_size(bg, AREA_W, LANE_H);
        lv_obj_set_pos(bg, 0, laneY(lane));
        lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(bg, onLaneClick, LV_EVENT_CLICKED, this);
        if (lane > 0) {
            m_lane[lane - 1] = bg;
            lv_obj_t* l = barLabel(bg, "");
            lv_obj_set_style_text_color(l, p.textSecondary, 0);
        }
    }

    m_beatBar = bar(m_area, theme::tint(Tint::Purple));
    lv_obj_set_pos(m_beatBar, 0, laneY(0));
    lv_obj_set_height(m_beatBar, LANE_H);
    lv_obj_add_flag(m_beatBar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m_beatBar, onLaneClick, LV_EVENT_CLICKED, this);
    m_beatLbl = barLabel(m_beatBar, "Beat");

    for (int i = 0; i < studio::MAX_CLIPS; i++) {
        m_clip[i] = bar(m_area, theme::tint(Tint::Orange));
        lv_obj_set_pos(m_clip[i], 0, laneY(i + 1));
        lv_obj_set_height(m_clip[i], LANE_H);
        lv_obj_set_style_border_color(m_clip[i], lv_color_white(), 0);
        lv_obj_set_style_border_width(m_clip[i], 0, 0);
        lv_obj_add_flag(m_clip[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(m_clip[i], reinterpret_cast<void*>(i));
        for (auto ev : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
            lv_obj_add_event_cb(m_clip[i], onClipEvent, ev, this);
        barLabel(m_clip[i], (String("Spur ") + (i + 1)).c_str());
    }

    m_head = lv_obj_create(m_area);
    lv_obj_remove_style_all(m_head);
    lv_obj_set_size(m_head, 2, lv_obj_get_height(m_area));
    lv_obj_set_style_bg_color(m_head, p.text, 0);
    lv_obj_set_style_bg_opa(m_head, LV_OPA_COVER, 0);
    lv_obj_clear_flag(m_head, LV_OBJ_FLAG_CLICKABLE);

    // Bedienleiste
    lv_obj_t* row = ui::box(tile);
    lv_obj_set_size(row, 228, 52);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    m_btnStart = ui::roundButton(row, LV_SYMBOL_PREV, 44, p.surfaceAlt, p.text, &font_caption);
    lv_obj_add_event_cb(m_btnStart, [](lv_event_t* e) {
        auto* a = static_cast<StudioApp*>(lv_event_get_user_data(e));
        a->m_cursor = 0;
        haptics::tap();
        a->tick();
    }, LV_EVENT_CLICKED, this);

    m_btnPlay = ui::roundButton(row, LV_SYMBOL_PLAY, 52, p.text, p.bg, &font_title);
    lv_obj_add_event_cb(m_btnPlay, [](lv_event_t* e) {
        static_cast<StudioApp*>(lv_event_get_user_data(e))->togglePlay();
    }, LV_EVENT_CLICKED, this);

    // Aufnahme: roter Punkt (wird beim Aufnehmen zum Quadrat)
    m_btnRec = ui::roundButton(row, "", 52, p.surfaceAlt, p.text, &font_caption);
    m_recDot = lv_obj_create(m_btnRec);
    lv_obj_remove_style_all(m_recDot);
    lv_obj_set_size(m_recDot, 20, 20);
    lv_obj_set_style_radius(m_recDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_recDot, theme::tint(Tint::Red), 0);
    lv_obj_set_style_bg_opa(m_recDot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(m_recDot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(m_recDot);
    lv_obj_add_event_cb(m_btnRec, [](lv_event_t* e) {
        static_cast<StudioApp*>(lv_event_get_user_data(e))->toggleRecord();
    }, LV_EVENT_CLICKED, this);

    m_btnDel = ui::roundButton(row, LV_SYMBOL_TRASH, 44, p.surfaceAlt, theme::tint(Tint::Red), &font_caption);
    lv_obj_add_event_cb(m_btnDel, [](lv_event_t* e) {
        auto* a = static_cast<StudioApp*>(lv_event_get_user_data(e));
        if (a->m_selected < 0 || studio::isPlaying()) return;
        studio::deleteClip(a->m_selected);
        a->m_selected = -1;
        haptics::tap();
        a->tick();
    }, LV_EVENT_CLICKED, this);

    m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<StudioApp*>(t->user_data)->tick(); }, 50, this);
    lv_timer_pause(m_timer);
}

void StudioApp::onShow() {
    m_rev = 0;
    tick();
    lv_timer_resume(m_timer);
}

void StudioApp::onHide() {
    lv_timer_pause(m_timer);
    if (studio::isPlaying()) studio::stop();
}

bool StudioApp::onButton() {
    if (studio::isRecording()) toggleRecord();
    else togglePlay();
    return true;
}

// --- Bedienung -----------------------------------------------------------------
void StudioApp::togglePlay() {
    if (studio::isPlaying()) {
        studio::stop();
    } else {
        if (studio::songLength() == 0) return;
        studio::play(m_cursor);
    }
    haptics::tap();
    tick();
}

void StudioApp::toggleRecord() {
    if (studio::isPlaying()) {
        bool wasRec = studio::isRecording();
        studio::stop();
        if (wasRec) haptics::success();
    } else if (!studio::record(m_cursor)) {
        haptics::success();
        m_msg = studio::clip(0).used && studio::clip(1).used && studio::clip(2).used ? "Alle Spuren belegt" : "Mikrofon belegt";
        m_msgUntil = millis() + 2500;
        return;
    } else {
        m_selected = -1;
        haptics::tap();
    }
    ui::setSwipeLocked(studio::isRecording());
    tick();
}

void StudioApp::onLaneClick(lv_event_t* e) {
    auto* a = static_cast<StudioApp*>(lv_event_get_user_data(e));
    if (studio::isPlaying()) return;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    lv_area_t ar;
    lv_obj_get_coords(a->m_area, &ar);
    a->m_cursor = a->samplesFor(pt.x - ar.x1);
    if (a->m_cursor > studio::songLength()) a->m_cursor = studio::songLength();
    a->m_selected = -1;
    a->m_rev = 0;
    a->tick();
}

void StudioApp::onClipEvent(lv_event_t* e) {
    auto* a = static_cast<StudioApp*>(lv_event_get_user_data(e));
    lv_obj_t* obj = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(obj);
    if (studio::isPlaying()) return;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);

    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            ui::setSwipeLocked(true);   // sonst wischt die Kachelansicht mit
            a->m_view = a->viewLength();
            a->m_dragIdx = idx;
            a->m_dragX0 = pt.x;
            a->m_dragOff0 = studio::clip(idx).offset;
            a->m_dragMoved = false;
            break;
        case LV_EVENT_PRESSING: {
            if (a->m_dragIdx != idx) break;
            lv_coord_t dx = pt.x - a->m_dragX0;
            if (abs(dx) > 4) a->m_dragMoved = true;
            if (!a->m_dragMoved) break;
            int64_t off = (int64_t)a->m_dragOff0 + (int64_t)dx * a->m_view / AREA_W;
            off = max<int64_t>(0, off) / SNAP * SNAP;
            lv_obj_set_x(obj, a->xFor((uint32_t)off));
            break;
        }
        default: {   // RELEASED / PRESS_LOST
            if (a->m_dragIdx != idx) break;
            ui::setSwipeLocked(false);
            if (a->m_dragMoved) {
                lv_coord_t dx = pt.x - a->m_dragX0;
                int64_t off = (int64_t)a->m_dragOff0 + (int64_t)dx * a->m_view / AREA_W;
                off = max<int64_t>(0, off) / SNAP * SNAP;
                studio::moveClip(idx, (uint32_t)off);
                a->m_selected = idx;
                haptics::tap();
            } else {
                a->m_selected = (a->m_selected == idx) ? -1 : idx;
            }
            a->m_dragIdx = -1;
            a->m_view = 0;
            a->m_rev = 0;
            a->tick();
            break;
        }
    }
}

// --- Anzeige -------------------------------------------------------------------
void StudioApp::layout() {
    const theme::Palette& p = theme::colors();
    // Beat
    if (studio::hasBeat()) {
        lv_obj_clear_flag(m_beatBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(m_beatBar, max<lv_coord_t>(xFor(studio::beatLength()), 8));
        ui::setText(m_beatLbl, studio::beatName().length() ? studio::beatName().c_str() : "Beat");
    } else {
        lv_obj_add_flag(m_beatBar, LV_OBJ_FLAG_HIDDEN);
    }
    // Spuren
    for (int i = 0; i < studio::MAX_CLIPS; i++) {
        const studio::Clip& c = studio::clip(i);
        bool rec = studio::recordingClip() == i;
        lv_obj_t* hint = lv_obj_get_child(m_lane[i], 0);
        if (c.used || rec) {
            lv_obj_clear_flag(m_clip[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(m_clip[i], theme::tint(rec ? Tint::Red : Tint::Orange), 0);
            lv_obj_set_style_border_width(m_clip[i], m_selected == i ? 2 : 0, 0);
            lv_obj_set_x(m_clip[i], xFor(c.offset));
            if (!rec) lv_obj_set_width(m_clip[i], max<lv_coord_t>(xFor(c.length), 10));
            ui::setText(hint, "");
        } else {
            lv_obj_add_flag(m_clip[i], LV_OBJ_FLAG_HIDDEN);
            ui::setText(hint, i == 0 && !studio::clip(1).used && !studio::clip(2).used ? "● zum Aufnehmen" : "");
        }
    }
    // Knöpfe
    bool playing = studio::isPlaying();
    bool recording = studio::isRecording();
    lv_label_set_text(lv_obj_get_child(m_btnPlay, 0), playing && !recording ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    lv_obj_set_style_radius(m_recDot, recording ? 4 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(m_recDot, recording ? 18 : 20, recording ? 18 : 20);
    bool canDel = m_selected >= 0 && !playing;
    lv_obj_set_style_opa(m_btnDel, canDel ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(m_btnPlay, studio::songLength() || playing ? LV_OPA_COVER : LV_OPA_40, 0);
    (void)p;
}

void StudioApp::tick() {
    uint32_t rev = studio::revision();
    if (rev != m_rev) {
        m_rev = rev;
        if (m_dragIdx < 0) layout();
    }

    bool playing = studio::isPlaying();
    uint32_t pos = playing ? studio::position() : m_cursor;
    int rc = studio::recordingClip();
    if (rc >= 0) {
        const studio::Clip& c = studio::clip(rc);
        lv_obj_set_width(m_clip[rc], max<lv_coord_t>(xFor(pos > c.offset ? pos - c.offset : 0), 4));
    }
    lv_obj_set_x(m_head, min<lv_coord_t>(xFor(pos), AREA_W - 2));

    // Kopfzeile
    int tp = studio::transferPercent();
    String head;
    lv_color_t hc = theme::colors().text;
    if (tp >= 0) {
        head = String("Beat lädt … ") + tp + " %";
    } else if (studio::isRecording()) {
        head = String("● Aufnahme  ") + fmtTime(pos - studio::clip(rc).offset);
        hc = theme::tint(Tint::Red);
    } else if (!studio::hasBeat()) {
        head = "Beat in WatchBridge laden";
        hc = theme::colors().textSecondary;
    } else if (m_selected >= 0) {
        head = String("Spur ") + (m_selected + 1) + " · ziehen / löschen";
    } else {
        head = "Studio";
    }
    if (millis() < m_msgUntil) {
        head = m_msg;
        hc = theme::tint(Tint::Orange);
    }
    ui::setText(m_header, head.c_str());
    lv_obj_set_style_text_color(m_header, hc, 0);

    String t = fmtTime(pos) + " / " + fmtTime(studio::songLength());
    ui::setText(m_timeLbl, t.c_str());

    if (!playing) ui::setSwipeLocked(m_dragIdx >= 0);
}
