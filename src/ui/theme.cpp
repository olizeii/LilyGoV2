#include "theme.h"

#include <vector>

#include "core/settings.h"

namespace theme {

static const Palette DARK = {
    .bg            = lv_color_hex(0x000000),
    .surface       = lv_color_hex(0x1C1C1E),
    .surfaceAlt    = lv_color_hex(0x2C2C2E),
    .text          = lv_color_hex(0xFFFFFF),
    .textSecondary = lv_color_hex(0x8E8E93),
    .separator     = lv_color_hex(0x38383A),
    .accent        = lv_color_hex(0x0A84FF),
    .accentAlt     = lv_color_hex(0xFF9F0A),
    .red           = lv_color_hex(0xFF453A),
    .green         = lv_color_hex(0x30D158),
};

static const Palette LIGHT = {
    .bg            = lv_color_hex(0xFFFFFF),
    .surface       = lv_color_hex(0xF2F2F7),
    .surfaceAlt    = lv_color_hex(0xE5E5EA),
    .text          = lv_color_hex(0x000000),
    .textSecondary = lv_color_hex(0x6C6C70),
    .separator     = lv_color_hex(0xC6C6C8),
    .accent        = lv_color_hex(0x007AFF),
    .accentAlt     = lv_color_hex(0xFF9500),
    .red           = lv_color_hex(0xFF3B30),
    .green         = lv_color_hex(0x34C759),
};

static lv_disp_t* s_disp = nullptr;
static bool s_dark = true;
static std::vector<std::function<void()>> s_listeners;

static lv_style_t s_screen, s_card, s_cardPressed, s_button, s_buttonAccent, s_pressed;
static lv_style_t s_textLarge, s_textTitle, s_textBody, s_textCaption, s_textAccent, s_textAccentAlt;
static lv_style_t s_lineText, s_lineSecondary, s_lineAccent;
static lv_style_t s_fillAccentAlt, s_arcTrack, s_arcIndicator;

const Palette& colors() { return s_dark ? DARK : LIGHT; }
bool isDark() { return s_dark; }

// Einmalig: farbunabhängige Eigenschaften
static void initStyles() {
    for (lv_style_t* s : {&s_screen, &s_card, &s_cardPressed, &s_button, &s_buttonAccent, &s_pressed,
                          &s_textLarge, &s_textTitle, &s_textBody, &s_textCaption, &s_textAccent,
                          &s_textAccentAlt, &s_lineText, &s_lineSecondary, &s_lineAccent,
                          &s_fillAccentAlt, &s_arcTrack, &s_arcIndicator}) {
        lv_style_init(s);
    }

    lv_style_set_bg_opa(&s_screen, LV_OPA_COVER);
    lv_style_set_border_width(&s_screen, 0);
    lv_style_set_radius(&s_screen, 0);
    lv_style_set_pad_all(&s_screen, 0);

    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, RADIUS_CARD);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, PAD);
    lv_style_set_pad_row(&s_card, 6);

    lv_style_set_bg_opa(&s_button, LV_OPA_COVER);
    lv_style_set_radius(&s_button, RADIUS_PILL);
    lv_style_set_border_width(&s_button, 0);
    lv_style_set_shadow_width(&s_button, 0);
    lv_style_set_pad_all(&s_button, 10);
    lv_style_set_text_font(&s_button, &lv_font_montserrat_16);

    lv_style_set_bg_opa(&s_buttonAccent, LV_OPA_COVER);
    lv_style_set_radius(&s_buttonAccent, RADIUS_PILL);
    lv_style_set_border_width(&s_buttonAccent, 0);
    lv_style_set_shadow_width(&s_buttonAccent, 0);
    lv_style_set_pad_all(&s_buttonAccent, 10);
    lv_style_set_text_color(&s_buttonAccent, lv_color_white());
    lv_style_set_text_font(&s_buttonAccent, &lv_font_montserrat_16);

    lv_style_set_opa(&s_pressed, LV_OPA_60);

    lv_style_set_text_font(&s_textLarge, &lv_font_montserrat_48);
    lv_style_set_text_letter_space(&s_textLarge, -1);
    lv_style_set_text_font(&s_textTitle, &lv_font_montserrat_24);
    lv_style_set_text_font(&s_textBody, &lv_font_montserrat_16);
    lv_style_set_text_font(&s_textCaption, &lv_font_montserrat_14);

    lv_style_set_line_rounded(&s_lineText, true);
    lv_style_set_line_width(&s_lineText, 5);
    lv_style_set_line_rounded(&s_lineSecondary, true);
    lv_style_set_line_width(&s_lineSecondary, 3);
    lv_style_set_line_rounded(&s_lineAccent, true);
    lv_style_set_line_width(&s_lineAccent, 2);

    lv_style_set_bg_opa(&s_fillAccentAlt, LV_OPA_COVER);
    lv_style_set_radius(&s_fillAccentAlt, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_fillAccentAlt, 0);

    lv_style_set_arc_width(&s_arcTrack, 8);
    lv_style_set_arc_rounded(&s_arcTrack, true);
    lv_style_set_bg_opa(&s_arcTrack, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_arcTrack, 0);
    lv_style_set_pad_all(&s_arcTrack, 0);
    lv_style_set_arc_width(&s_arcIndicator, 8);
    lv_style_set_arc_rounded(&s_arcIndicator, true);
}

// Bei jedem Wechsel: Farben
static void applyPalette() {
    const Palette& p = colors();

    lv_style_set_bg_color(&s_screen, p.bg);
    lv_style_set_text_color(&s_screen, p.text);

    lv_style_set_bg_color(&s_card, p.surface);
    lv_style_set_text_color(&s_card, p.text);
    lv_style_set_bg_color(&s_cardPressed, p.surfaceAlt);

    lv_style_set_bg_color(&s_button, p.surfaceAlt);
    lv_style_set_text_color(&s_button, p.text);
    lv_style_set_bg_color(&s_buttonAccent, p.accent);

    lv_style_set_text_color(&s_textLarge, p.text);
    lv_style_set_text_color(&s_textTitle, p.text);
    lv_style_set_text_color(&s_textBody, p.text);
    lv_style_set_text_color(&s_textCaption, p.textSecondary);
    lv_style_set_text_color(&s_textAccent, p.accent);
    lv_style_set_text_color(&s_textAccentAlt, p.accentAlt);

    lv_style_set_line_color(&s_lineText, p.text);
    lv_style_set_line_color(&s_lineSecondary, p.textSecondary);
    lv_style_set_line_color(&s_lineAccent, p.accentAlt);

    lv_style_set_bg_color(&s_fillAccentAlt, p.accentAlt);
    lv_style_set_arc_color(&s_arcTrack, p.surfaceAlt);
    lv_style_set_arc_color(&s_arcIndicator, p.accentAlt);

    // LVGL-Standardtheme für eingebaute Widgets (Switch, Liste, Msgbox ...)
    lv_theme_t* th = lv_theme_default_init(s_disp, p.accent, p.red, s_dark, &lv_font_montserrat_16);
    lv_disp_set_theme(s_disp, th);
    lv_obj_report_style_change(nullptr);  // alles neu zeichnen
}

void begin(lv_disp_t* disp, bool dark) {
    s_disp = disp;
    s_dark = dark;
    initStyles();
    applyPalette();
}

void setDark(bool dark) {
    if (dark == s_dark) return;
    s_dark = dark;
    settings::setDarkMode(dark);
    applyPalette();
    for (auto& l : s_listeners) l();
}

void toggle() { setDark(!s_dark); }

void onChange(std::function<void()> listener) { s_listeners.push_back(std::move(listener)); }

lv_style_t* screen()        { return &s_screen; }
lv_style_t* card()          { return &s_card; }
lv_style_t* cardPressed()   { return &s_cardPressed; }
lv_style_t* button()        { return &s_button; }
lv_style_t* buttonAccent()  { return &s_buttonAccent; }
lv_style_t* pressed()       { return &s_pressed; }
lv_style_t* textLarge()     { return &s_textLarge; }
lv_style_t* textTitle()     { return &s_textTitle; }
lv_style_t* textBody()      { return &s_textBody; }
lv_style_t* textCaption()   { return &s_textCaption; }
lv_style_t* textAccent()    { return &s_textAccent; }
lv_style_t* textAccentAlt() { return &s_textAccentAlt; }
lv_style_t* lineText()      { return &s_lineText; }
lv_style_t* lineSecondary() { return &s_lineSecondary; }
lv_style_t* lineAccent()    { return &s_lineAccent; }
lv_style_t* fillAccentAlt() { return &s_fillAccentAlt; }
lv_style_t* arcTrack()      { return &s_arcTrack; }
lv_style_t* arcIndicator()  { return &s_arcIndicator; }

lv_obj_t* label(lv_obj_t* parent, lv_style_t* style, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_add_style(l, style, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t* cardObj(lv_obj_t* parent) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &s_card, 0);
    lv_obj_add_style(c, &s_cardPressed, LV_STATE_PRESSED);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

}  // namespace theme
