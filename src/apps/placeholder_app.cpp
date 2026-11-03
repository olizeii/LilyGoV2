#include "placeholder_app.h"

#include "ui/theme.h"

void PlaceholderApp::create(lv_obj_t* tile) {
    lv_obj_t* card = theme::cardObj(tile);
    lv_obj_set_size(card, 120, 120);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -18);

    lv_obj_t* icon = theme::label(card, theme::textTitle(), m_symbol);
    lv_obj_add_style(icon, theme::textAccent(), 0);
    lv_obj_center(icon);

    lv_obj_t* title = theme::label(tile, theme::textBody(), m_title);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 62);

    lv_obj_t* hint = theme::label(tile, theme::textCaption(), "Folgt bald");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 86);
}
