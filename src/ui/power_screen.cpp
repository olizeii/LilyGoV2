#include "power_screen.h"

#include <lvgl.h>

#include "core/activity.h"
#include "hal/haptics.h"
#include "hal/power.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace powerscreen {
namespace {

lv_obj_t* s_root = nullptr;

void doShutdown() {
    // Letzte Anzeige, dann aus (schwarze Fläche über alles – nichts löschen, wir sind im Klick-Callback)
    lv_obj_t* cover = lv_obj_create(s_root);
    lv_obj_remove_style_all(cover);
    lv_obj_set_size(cover, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cover, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
    lv_obj_t* l = lv_label_create(cover);
    lv_obj_set_style_text_font(l, &font_title, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, "Bis bald");
    lv_obj_center(l);
    lv_refr_now(nullptr);

    activity::flush();   // Schritte sichern
    haptics::tap();
    delay(700);
    power::shutdown();
}

}  // namespace

bool isOpen() { return s_root != nullptr; }

void close() {
    if (!s_root) return;
    lv_obj_del(s_root);
    s_root = nullptr;
}

void open() {
    if (s_root) return;
    haptics::tap();

    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);  // nichts darunter antippen
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(s_root);
    lv_obj_set_style_text_font(title, &font_headline, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Ausschalten?");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    // Großer roter Knopf – wie der Ausschalt-Regler bei watchOS
    lv_obj_t* off = ui::roundButton(s_root, LV_SYMBOL_POWER, 92, theme::tint(theme::Tint::Red), lv_color_white(),
                                    &font_title);
    lv_obj_align(off, LV_ALIGN_CENTER, 0, 2);
    lv_obj_add_event_cb(off, [](lv_event_t*) { doShutdown(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(hint, &font_caption, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8E8E93), 0);
    lv_label_set_text(hint, "Einschalten: Taste 1 s halten");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -58);

    lv_obj_t* cancel = lv_btn_create(s_root);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_size(cancel, 140, 40);
    lv_obj_set_style_radius(cancel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x3A3A3C), LV_STATE_PRESSED);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_t* cl = lv_label_create(cancel);
    lv_obj_set_style_text_font(cl, &font_body, 0);
    lv_obj_set_style_text_color(cl, lv_color_white(), 0);
    lv_label_set_text(cl, "Abbrechen");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);
}

}  // namespace powerscreen
