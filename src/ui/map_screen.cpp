#include "map_screen.h"

#include <lvgl.h>

#include "core/data_store.h"
#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace mapscreen {

static constexpr uint32_t LEASE_MS = 20000;
static constexpr uint8_t ZOOM_MIN = 12, ZOOM_MAX = 18;

static lv_obj_t* s_root     = nullptr;
static lv_obj_t* s_img      = nullptr;
static lv_obj_t* s_status   = nullptr;  // "Karte wird geladen …"
static lv_obj_t* s_nav      = nullptr;  // Banner oben
static lv_obj_t* s_navIcon  = nullptr;
static lv_obj_t* s_navDist  = nullptr;
static lv_obj_t* s_navText  = nullptr;
static lv_obj_t* s_eta      = nullptr;
static lv_timer_t* s_lease  = nullptr;
static lv_img_dsc_t s_dsc;
static uint8_t s_zoom = 16;

const char* maneuverIcon(const String& m) {
    if (m.indexOf("left") >= 0)  return ICON_ARROW_LEFT;
    if (m.indexOf("right") >= 0) return ICON_ARROW_RIGHT;
    if (m == "uturn")            return ICON_UTURN;
    if (m == "roundabout")       return ICON_ROUNDABOUT;
    if (m == "arrive")           return ICON_FLAG;
    return ICON_ARROW_UP;
}

static void request() {
    char arg[16];
    snprintf(arg, sizeof(arg), "%u|%d", s_zoom, theme::isDark() ? 1 : 0);
    store::sendToPhone("MAP_REQ", arg);
}

static void showMap() {
    if (!s_root) return;
    const MapImage& m = store::data().map;
    if (!m.data) {
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        ui::setText(s_status, store::data().bleConnected ? "Karte wird geladen " "\xE2\x80\xA6"
                                                         : "Handy nicht verbunden");
        lv_obj_center(s_status);
        return;
    }
    lv_img_cache_invalidate_src(&s_dsc);
    s_dsc.header.always_zero = 0;
    s_dsc.header.cf = LV_IMG_CF_INDEXED_4BIT;
    s_dsc.header.w  = m.w;
    s_dsc.header.h  = m.h;
    s_dsc.data_size = m.size;
    s_dsc.data      = m.data;
    lv_img_set_src(s_img, &s_dsc);
    lv_obj_center(s_img);
    lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
}

static void showNav() {
    if (!s_root) return;
    const NavState& n = store::data().nav;
    if (!n.active) {
        lv_obj_add_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eta, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    ui::setText(s_navIcon, maneuverIcon(n.maneuver));
    ui::setText(s_navDist, n.distance.c_str());
    ui::setText(s_navText, n.instruction.c_str());
    if (n.eta.length()) {
        lv_obj_clear_flag(s_eta, LV_OBJ_FLAG_HIDDEN);
        ui::setText(lv_obj_get_child(s_eta, 0), n.eta.c_str());
    } else {
        lv_obj_add_flag(s_eta, LV_OBJ_FLAG_HIDDEN);
    }
}

// Halbtransparente runde Bedienknöpfe über der Karte
static lv_obj_t* overlayButton(const char* sym, lv_align_t align, lv_coord_t x, lv_coord_t y) {
    lv_obj_t* b = ui::roundButton(s_root, sym, 38, lv_color_black(), lv_color_white(), &font_caption);
    lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
    lv_obj_set_ext_click_area(b, 8);
    lv_obj_align(b, align, x, y);
    return b;
}

void open() {
    if (s_root) return;
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_add_style(s_root, theme::screen(), 0);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_img = lv_img_create(s_root);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    s_status = theme::label(s_root, theme::textCaption(), "");

    // Positionspunkt (Karte ist auf die Position zentriert)
    lv_obj_t* halo = ui::box(s_root);
    lv_obj_set_size(halo, 22, 22);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(halo, theme::tint(theme::Tint::Blue), 0);
    lv_obj_center(halo);
    lv_obj_t* dot = ui::box(s_root);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, theme::tint(theme::Tint::Blue), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_set_style_border_color(dot, lv_color_white(), 0);
    lv_obj_center(dot);

    // Navigationsbanner oben
    s_nav = ui::box(s_root);
    lv_obj_set_size(s_nav, 224, LV_SIZE_CONTENT);
    lv_obj_align(s_nav, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(s_nav, 18, 0);
    lv_obj_set_style_bg_opa(s_nav, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(s_nav, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_pad_all(s_nav, 10, 0);
    lv_obj_set_flex_flow(s_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_nav, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_nav, 10, 0);
    lv_obj_t* arrow = theme::appIcon(s_nav, ICON_ARROW_UP, theme::tint(theme::Tint::Green), 48);
    lv_obj_set_style_text_font(lv_obj_get_child(arrow, 0), &font_title, 0);
    s_navIcon = lv_obj_get_child(arrow, 0);
    lv_obj_t* col = ui::box(s_nav);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    s_navDist = lv_label_create(col);
    lv_obj_set_style_text_font(s_navDist, &font_title, 0);
    lv_obj_set_style_text_color(s_navDist, lv_color_white(), 0);
    s_navText = lv_label_create(col);
    lv_obj_set_style_text_font(s_navText, &font_caption, 0);
    lv_obj_set_style_text_color(s_navText, lv_color_hex(0xEBEBF5), 0);
    lv_obj_set_width(s_navText, LV_PCT(100));
    lv_label_set_long_mode(s_navText, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_height(s_navText, 38, 0);
    lv_obj_add_flag(s_nav, LV_OBJ_FLAG_HIDDEN);

    // Ankunft (Pille unten Mitte)
    s_eta = ui::box(s_root);
    lv_obj_set_size(s_eta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(s_eta, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_eta, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(s_eta, lv_color_black(), 0);
    lv_obj_set_style_pad_hor(s_eta, 12, 0);
    lv_obj_set_style_pad_ver(s_eta, 6, 0);
    lv_obj_t* etaL = lv_label_create(s_eta);
    lv_obj_set_style_text_font(etaL, &font_caption, 0);
    lv_obj_set_style_text_color(etaL, lv_color_white(), 0);
    lv_obj_align(s_eta, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_obj_add_flag(s_eta, LV_OBJ_FLAG_HIDDEN);

    // Bedienung unten: schließen | − | +
    lv_obj_t* closeBtn = overlayButton(ICON_CLOSE, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lv_obj_add_event_cb(closeBtn, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* zoomOut = overlayButton("\xE2\x88\x92", LV_ALIGN_BOTTOM_RIGHT, -60, -10);
    lv_obj_set_style_text_font(lv_obj_get_child(zoomOut, 0), &font_title, 0);
    lv_obj_add_event_cb(
        zoomOut,
        [](lv_event_t*) {
            if (s_zoom > ZOOM_MIN) s_zoom--;
            haptics::tap();
            request();
        },
        LV_EVENT_CLICKED, nullptr);
    lv_obj_t* zoomIn = overlayButton("+", LV_ALIGN_BOTTOM_RIGHT, -14, -10);
    lv_obj_set_style_text_font(lv_obj_get_child(zoomIn, 0), &font_title, 0);
    lv_obj_add_event_cb(
        zoomIn,
        [](lv_event_t*) {
            if (s_zoom < ZOOM_MAX) s_zoom++;
            haptics::tap();
            request();
        },
        LV_EVENT_CLICKED, nullptr);

    showMap();
    showNav();
    request();
    s_lease = lv_timer_create([](lv_timer_t*) { request(); }, LEASE_MS, nullptr);
    lv_obj_fade_in(s_root, 160, 0);
}

void close() {
    if (!s_root) return;
    if (s_lease) lv_timer_del(s_lease);
    s_lease = nullptr;
    store::sendToPhone("MAP_STOP");
    lv_obj_del_async(s_root);
    s_root = nullptr;
}

bool isOpen() { return s_root != nullptr; }

void begin() {
    store::subscribe(Topic::Map, [] { showMap(); });
    store::subscribe(Topic::Nav, [] { showNav(); });
    store::subscribe(Topic::Connection, [] { showMap(); });
}

}  // namespace mapscreen
