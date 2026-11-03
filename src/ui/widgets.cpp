#include "widgets.h"

#include <vector>

namespace ui {

lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

void setText(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t* roundButton(lv_obj_t* parent, const char* symbol, lv_coord_t size, lv_color_t bg, lv_color_t fg,
                      const lv_font_t* font) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_add_style(b, theme::pressed(), LV_STATE_PRESSED);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, fg, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t* emptyState(lv_obj_t* parent, const char* icon, lv_color_t tint, const char* title,
                     const char* subtitle) {
    lv_obj_t* c = box(parent);
    lv_obj_set_size(c, 210, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 6, 0);
    lv_obj_center(c);

    lv_obj_t* ic = theme::appIcon(c, icon, tint, 72);
    lv_obj_set_style_text_font(lv_obj_get_child(ic, 0), &font_title, 0);
    lv_obj_set_style_pad_bottom(ic, 4, 0);

    lv_obj_t* t = theme::label(c, theme::textHeadline(), title);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* s = theme::label(c, theme::textCaption(), subtitle);
    lv_obj_set_width(s, LV_PCT(100));
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    return c;
}

// --- Sheets ------------------------------------------------------------------

struct SheetEntry {
    lv_obj_t* root;
    std::function<void()> onClose;
};
static std::vector<SheetEntry> s_sheets;

Sheet openSheet(const char* title, std::function<void()> onClose) {
    Sheet s;
    s.root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s.root);
    lv_obj_add_style(s.root, theme::screen(), 0);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s.root, LV_OBJ_FLAG_SCROLLABLE);  // bleibt CLICKABLE -> blockiert Tileview

    s.header = box(s.root);
    lv_obj_set_size(s.header, LV_PCT(100), 50);
    lv_obj_set_flex_flow(s.header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s.header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(s.header, 14, 0);
    lv_obj_set_style_pad_column(s.header, 10, 0);

    lv_obj_t* close = theme::pillButton(s.header, ICON_CLOSE, false);
    lv_obj_set_size(close, 34, 34);
    lv_obj_set_style_pad_all(close, 0, 0);
    lv_obj_set_style_text_font(close, &font_caption, 0);
    lv_obj_set_ext_click_area(close, 12);
    lv_obj_t* root = s.root;
    lv_obj_add_event_cb(
        close, [](lv_event_t* e) { closeSheet(static_cast<lv_obj_t*>(lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, root);

    s.title = theme::label(s.header, theme::textHeadline(), title);
    lv_obj_set_flex_grow(s.title, 1);
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_DOT);

    s.content = lv_obj_create(s.root);
    lv_obj_remove_style_all(s.content);
    lv_obj_set_size(s.content, LV_PCT(100), 190);
    lv_obj_set_pos(s.content, 0, 50);
    lv_obj_set_flex_flow(s.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(s.content, 14, 0);
    lv_obj_set_style_pad_bottom(s.content, 16, 0);
    lv_obj_set_style_pad_row(s.content, 8, 0);
    lv_obj_set_scroll_dir(s.content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s.content, LV_SCROLLBAR_MODE_OFF);

    s_sheets.push_back({s.root, std::move(onClose)});
    lv_obj_fade_in(s.root, 160, 0);
    return s;
}

void closeSheet(lv_obj_t* root) {
    for (auto it = s_sheets.begin(); it != s_sheets.end(); ++it) {
        if (it->root != root) continue;
        auto cb = std::move(it->onClose);
        s_sheets.erase(it);
        lv_obj_del_async(root);  // async: wir kommen evtl. aus einem Kind-Event
        if (cb) cb();
        return;
    }
}

void closeAllSheets() {
    while (!s_sheets.empty()) closeSheet(s_sheets.back().root);
}

bool sheetOpen() { return !s_sheets.empty(); }

}  // namespace ui
