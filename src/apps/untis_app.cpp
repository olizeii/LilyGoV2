#include "untis_app.h"

#include "core/data_store.h"
#include "hal/rtc_clock.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

int toMinutes(const String& hhmm) {
    int c = hhmm.indexOf(':');
    if (c < 0) return -1;
    return hhmm.substring(0, c).toInt() * 60 + hhmm.substring(c + 1).toInt();
}

lv_color_t statusColor(char s) {
    switch (s) {
        case 'C': return theme::colors().red;
        case 'S': return theme::tint(theme::Tint::Orange);
        case 'E': return theme::tint(theme::Tint::Purple);
        default:  return theme::colors().accent;
    }
}

}  // namespace

void UntisApp::create(lv_obj_t* tile) {
    lv_obj_t* cap = theme::label(tile, theme::textCaption(), "STUNDENPLAN");
    lv_obj_set_style_text_color(cap, theme::tint(theme::Tint::Red), 0);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_set_pos(cap, 18, 12);
    m_title = theme::label(tile, theme::textHeadline(), "Heute");
    lv_obj_set_pos(m_title, 18, 30);

    m_list = lv_obj_create(tile);
    lv_obj_remove_style_all(m_list);
    lv_obj_set_size(m_list, 240, 240 - 58);
    lv_obj_set_pos(m_list, 0, 58);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(m_list, 10, 0);
    lv_obj_set_style_pad_bottom(m_list, 14, 0);
    lv_obj_set_style_pad_row(m_list, 6, 0);
    lv_obj_set_scroll_dir(m_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_list, LV_SCROLLBAR_MODE_OFF);

    m_empty = ui::emptyState(tile, ICON_CALENDAR, theme::tint(theme::Tint::Red), "Keine Stunden",
                             "Heute ist nichts eingetragen");

    store::subscribe(Topic::Untis, [this] { rebuild(); });
    store::subscribe(Topic::Connection, [this] { rebuild(); });
    theme::onChange([this] { rebuild(); });
    rebuild();
}

void UntisApp::onShow() {
    store::sendToPhone("UNTIS_REQ");  // Handy drosselt selbst (max. alle paar Minuten)
    scrollToNow(false);
}

void UntisApp::onTick() {
    // Hervorhebung wandert mit, wenn eine Stunde endet
    if (currentIndex() != m_highlighted) rebuild();
}

int UntisApp::currentIndex() const {
    if (!rtc_clock::isValid()) return -1;
    struct tm t = rtc_clock::now();
    int now = t.tm_hour * 60 + t.tm_min;
    const auto& u = store::data().untis;
    for (size_t i = 0; i < u.size(); i++) {
        if (u[i].status == 'C') continue;
        if (now < toMinutes(u[i].end)) return int(i);
    }
    return -1;
}

void UntisApp::rebuild() {
    const WatchData& d = store::data();
    const theme::Palette& p = theme::colors();
    lv_obj_clean(m_list);
    ui::setText(m_title, d.untisTitle.length() ? d.untisTitle.c_str() : "Heute");

    if (d.untis.empty()) {
        lv_obj_add_flag(m_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
        ui::setText(lv_obj_get_child(m_empty, 2),
                    d.bleConnected ? "Heute ist nichts eingetragen" : "Handy nicht verbunden");
        m_highlighted = -1;
        return;
    }
    lv_obj_add_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_list, LV_OBJ_FLAG_HIDDEN);

    int cur = currentIndex();
    m_highlighted = cur;
    int nowMin = -1;
    if (rtc_clock::isValid()) {
        struct tm t = rtc_clock::now();
        nowMin = t.tm_hour * 60 + t.tm_min;
    }

    for (size_t i = 0; i < d.untis.size(); i++) {
        const Lesson& l = d.untis[i];
        bool cancelled = l.status == 'C';
        bool running   = int(i) == cur && nowMin >= toMinutes(l.start);

        lv_obj_t* row = theme::cardObj(m_list);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_pad_left(row, 14, 0);
        lv_obj_set_style_min_height(row, 50, 0);
        if (running) lv_obj_set_style_bg_color(row, p.surfaceAlt, 0);

        // Farbleiste links (wie Kalender-Ereignisse)
        lv_obj_t* bar = ui::box(row);
        lv_obj_set_size(bar, 4, 34);  // feste Höhe: Zeile hat LV_SIZE_CONTENT
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, statusColor(l.status), 0);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, -8, 0);

        // Zeit
        lv_obj_t* tStart = theme::label(row, theme::textCaption(), l.start.c_str());
        lv_obj_set_style_text_color(tStart, p.text, 0);
        lv_obj_set_pos(tStart, 0, 0);
        lv_obj_t* tEnd = theme::label(row, theme::textCaption(), l.end.c_str());
        lv_obj_set_pos(tEnd, 0, 18);

        // Fach + Details
        lv_obj_t* subj = theme::label(row, theme::textHeadline(), l.subject.c_str());
        lv_label_set_long_mode(subj, LV_LABEL_LONG_DOT);
        lv_obj_set_width(subj, running ? 92 : 134);
        lv_obj_set_pos(subj, 52, -2);
        if (cancelled) {
            lv_obj_set_style_text_decor(subj, LV_TEXT_DECOR_STRIKETHROUGH, 0);
            lv_obj_set_style_text_color(subj, p.textSecondary, 0);
        }

        String detail;
        if (l.info.length()) {
            detail = l.info;
        } else if (cancelled) {
            detail = "Entfällt";
        } else {
            detail = l.room;
            if (l.teacher.length()) detail += (detail.length() ? " · " : "") + l.teacher;
        }
        lv_obj_t* det = theme::label(row, theme::textCaption(), detail.c_str());
        lv_label_set_long_mode(det, LV_LABEL_LONG_DOT);
        lv_obj_set_width(det, 134);
        lv_obj_set_pos(det, 52, 18);
        if (l.status == 'S' || l.status == 'C') lv_obj_set_style_text_color(det, statusColor(l.status), 0);

        if (running) {
            lv_obj_t* now = theme::label(row, theme::textCaption(), "JETZT");
            lv_obj_add_style(now, theme::textAccent(), 0);
            lv_obj_align(now, LV_ALIGN_TOP_RIGHT, 0, 0);
        }
    }
    scrollToNow(false);
}

void UntisApp::scrollToNow(bool anim) {
    int cur = currentIndex();
    if (cur < 0 || uint32_t(cur) >= lv_obj_get_child_cnt(m_list)) return;
    lv_obj_update_layout(m_list);
    lv_obj_t* row = lv_obj_get_child(m_list, cur);
    lv_coord_t y = lv_obj_get_y(row) - 6;
    lv_obj_scroll_to_y(m_list, y < 0 ? 0 : y, anim ? LV_ANIM_ON : LV_ANIM_OFF);
}
