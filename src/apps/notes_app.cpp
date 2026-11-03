#include "notes_app.h"

#include "core/data_store.h"
#include "ui/theme.h"
#include "ui/widgets.h"

void NotesApp::create(lv_obj_t* tile) {
    lv_obj_t* cap = theme::label(tile, theme::textCaption(), "NOTIZEN");
    lv_obj_set_style_text_color(cap, theme::tint(theme::Tint::Yellow), 0);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_set_pos(cap, 18, 12);
    m_count = theme::label(tile, theme::textHeadline(), "");
    lv_obj_set_pos(m_count, 18, 30);

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

    m_empty = ui::emptyState(tile, ICON_NOTE, theme::tint(theme::Tint::Yellow), "Keine Notizen",
                             "In WatchBridge Notizen für die Uhr auswählen");
    lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(m_empty, 0), 0), lv_color_black(), 0);

    store::subscribe(Topic::Notes, [this] { rebuild(); });
    rebuild();
}

void NotesApp::rebuild() {
    const auto& notes = store::data().notes;
    lv_obj_clean(m_list);

    if (notes.empty()) {
        ui::setText(m_count, "");
        lv_obj_add_flag(m_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_list, LV_OBJ_FLAG_HIDDEN);
    char buf[24];
    snprintf(buf, sizeof(buf), notes.size() == 1 ? "1 Notiz" : "%u Notizen", unsigned(notes.size()));
    ui::setText(m_count, buf);

    for (size_t i = 0; i < notes.size(); i++) {
        const Note& n = notes[i];
        lv_obj_t* card = theme::cardObj(m_list);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 2, 0);

        lv_obj_t* title = theme::label(card, theme::textHeadline(), n.title.length() ? n.title.c_str() : "Ohne Titel");
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, LV_PCT(100));

        // Vorschau: erste Zeile des Texts
        String preview = n.body;
        int nl = preview.indexOf('\n');
        if (nl >= 0) preview = preview.substring(0, nl);
        lv_obj_t* prev = theme::label(card, theme::textCaption(), preview.c_str());
        lv_label_set_long_mode(prev, LV_LABEL_LONG_DOT);
        lv_obj_set_width(prev, LV_PCT(100));
        if (!preview.length()) lv_obj_add_flag(prev, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(
            card, [](lv_event_t* e) { NotesApp::openNote(size_t(uintptr_t(lv_event_get_user_data(e)))); },
            LV_EVENT_CLICKED, reinterpret_cast<void*>(uintptr_t(i)));
    }
}

void NotesApp::openNote(size_t index) {
    const auto& notes = store::data().notes;
    if (index >= notes.size()) return;
    const Note& n = notes[index];
    ui::Sheet s = ui::openSheet(n.title.length() ? n.title.c_str() : "Notiz");
    lv_obj_t* body = theme::label(s.content, theme::textBody(), n.body.c_str());
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
}
