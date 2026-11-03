#include "placeholder_app.h"

void PlaceholderApp::create(lv_obj_t* tile) {
    lv_obj_t* icon = theme::appIcon(tile, m_symbol, theme::tint(m_tint), 96, m_darkGlyph);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -26);

    lv_obj_t* title = theme::label(tile, theme::textHeadline(), m_title);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 46);

    lv_obj_t* hint = theme::label(tile, theme::textCaption(), "Bald verfügbar");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 70);
}
