#include "banner.h"

#include <lvgl.h>

#include "apps/messages_app.h"
#include "core/data_store.h"
#include "hal/display.h"
#include "hal/haptics.h"
#include "ui/map_screen.h"
#include "ui/theme.h"
#include "ui/ui_core.h"
#include "ui/widgets.h"

namespace banner {

static constexpr uint32_t SHOW_MS = 5000;

static lv_obj_t* s_banner  = nullptr;
static lv_timer_t* s_timer = nullptr;
static String s_key;          // Mitteilung, die beim Antippen geöffnet wird ("" = Karte)
static String s_lastNavText;  // nur bei neuer Anweisung melden, nicht bei jeder Distanz

static void hide() {
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    if (s_banner) {
        lv_obj_del_async(s_banner);
        s_banner = nullptr;
    }
}

static void slideIn(lv_obj_t* obj) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, -110, 8);
    lv_anim_set_time(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t*>(o), v); });
    lv_anim_start(&a);
}

// Gemeinsames Gerüst: dunkle Plattform oben mit Icon links und zwei Zeilen
static lv_obj_t* show(lv_obj_t** iconSlot) {
    if (ui::isSwipeLocked()) return nullptr;  // Spiel läuft -> nicht stören
    hide();

    if (display::isAsleep()) {
        ui::onWake();
        display::wake();
    }
    haptics::tap();

    const theme::Palette& p = theme::colors();
    s_banner = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_banner);
    lv_obj_set_size(s_banner, 228, LV_SIZE_CONTENT);
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(s_banner, 22, 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_banner, p.surfaceAlt, 0);
    lv_obj_set_style_shadow_width(s_banner, 24, 0);
    lv_obj_set_style_shadow_opa(s_banner, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(s_banner, 12, 0);
    lv_obj_set_style_pad_column(s_banner, 10, 0);
    lv_obj_set_flex_flow(s_banner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_banner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(s_banner, theme::pressed(), LV_STATE_PRESSED);
    lv_obj_move_foreground(s_banner);

    *iconSlot = s_banner;  // Aufrufer hängt sein Icon als erstes Kind ein

    lv_obj_add_event_cb(
        s_banner,
        [](lv_event_t*) {
            String key = s_key;
            hide();
            if (key.length()) MessagesApp::openDetail(key);
            else              mapscreen::open();
        },
        LV_EVENT_CLICKED, nullptr);

    // hide() löscht den Timer aus dem eigenen Callback heraus – das erlaubt LVGL
    s_timer = lv_timer_create([](lv_timer_t*) { hide(); }, SHOW_MS, nullptr);
    return s_banner;  // Textspalte folgt nach dem Icon (finish())
}

static void finish(const char* caption, const char* line1, const char* line2) {
    lv_obj_t* col = ui::box(s_banner);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    if (caption && *caption) {
        lv_obj_t* c = theme::label(col, theme::textCaption(), caption);
        lv_label_set_long_mode(c, LV_LABEL_LONG_DOT);
        lv_obj_set_width(c, LV_PCT(100));
    }
    lv_obj_t* l1 = theme::label(col, theme::textHeadline(), line1);
    lv_label_set_long_mode(l1, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l1, LV_PCT(100));
    if (line2 && *line2) {
        lv_obj_t* l2 = theme::label(col, theme::textBody(), line2);
        lv_label_set_long_mode(l2, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l2, LV_PCT(100), 44);  // max. 2 Zeilen
    }
    lv_timer_reset(s_timer);
    slideIn(s_banner);
}

static void onAlert() {
    const PhoneMessage& m = store::data().alert;
    lv_obj_t* parent = nullptr;
    if (!show(&parent)) return;
    s_key = m.key;
    MessagesApp::appBadge(parent, m.app, 36);
    finish(m.app.c_str(), m.title.c_str(), m.body.c_str());
}

static void onNav() {
    const NavState& n = store::data().nav;
    if (!n.active) {
        s_lastNavText = "";
        return;
    }
    if (n.instruction == s_lastNavText) return;  // nur Distanz geändert
    s_lastNavText = n.instruction;
    if (mapscreen::isOpen()) {  // Karte zeigt das Banner selbst – nur vibrieren
        haptics::tap();
        return;
    }
    lv_obj_t* parent = nullptr;
    if (!show(&parent)) return;
    s_key = "";
    lv_obj_t* ic = theme::appIcon(parent, mapscreen::maneuverIcon(n.maneuver), theme::tint(theme::Tint::Green), 44);
    lv_obj_set_style_text_font(lv_obj_get_child(ic, 0), &font_title, 0);
    finish(n.distance.c_str(), n.instruction.c_str(), nullptr);
}

void begin() {
    store::subscribe(Topic::Alert, [] { onAlert(); });
    store::subscribe(Topic::Nav, [] { onNav(); });
}

}  // namespace banner
