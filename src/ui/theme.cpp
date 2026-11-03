#include "theme.h"

#include <vector>

#include "core/settings.h"

namespace theme {

// Kontraste (WCAG) gegen bg/surface geprüft:
//  Dark : textSecondary 8E8E93 auf 000000 = 6.2:1, auf 1C1C1E = 5.0:1
//  Light: textSecondary 6C6C70 auf F2F2F7 = 4.7:1, auf FFFFFF = 5.3:1
//         accentAlt C93400 (Apple "accessible orange") auf F2F2F7 = 4.9:1
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
    .bg            = lv_color_hex(0xF2F2F7),
    .surface       = lv_color_hex(0xFFFFFF),
    .surfaceAlt    = lv_color_hex(0xE5E5EA),
    .text          = lv_color_hex(0x000000),
    .textSecondary = lv_color_hex(0x6C6C70),
    .separator     = lv_color_hex(0xC6C6C8),
    .accent        = lv_color_hex(0x007AFF),
    .accentAlt     = lv_color_hex(0xC93400),
    .red           = lv_color_hex(0xFF3B30),
    .green         = lv_color_hex(0x34C759),
};

lv_color_t tint(Tint t) {
    switch (t) {
        case Tint::Red:     return lv_color_hex(0xFF453A);
        case Tint::Orange:  return lv_color_hex(0xFF9F0A);
        case Tint::Yellow:  return lv_color_hex(0xFFD60A);
        case Tint::Green:   return lv_color_hex(0x30D158);
        case Tint::Mint:    return lv_color_hex(0x63E6E2);
        case Tint::Teal:    return lv_color_hex(0x40C8E0);
        case Tint::Cyan:    return lv_color_hex(0x64D2FF);
        case Tint::Blue:    return lv_color_hex(0x0A84FF);
        case Tint::Indigo:  return lv_color_hex(0x5E5CE6);
        case Tint::Purple:  return lv_color_hex(0xBF5AF2);
        case Tint::Pink:    return lv_color_hex(0xFF375F);
        case Tint::Gray:    return lv_color_hex(0x8E8E93);
        case Tint::Spotify: return lv_color_hex(0x1ED760);
    }
    return lv_color_hex(0x8E8E93);
}

static lv_disp_t* s_disp = nullptr;
static bool s_dark = true;
static std::vector<std::function<void()>> s_listeners;

static lv_style_t s_screen, s_card, s_cardPressed, s_button, s_buttonAccent, s_pressed;
static lv_style_t s_textTimeXL, s_textTime, s_textLarge, s_textTitle, s_textHeadline, s_textBody,
    s_textCaption, s_textAccent, s_textAccentAlt;
static lv_style_t s_lineText, s_lineSecondary, s_lineAccent;
static lv_style_t s_fillAccentAlt, s_fillBg, s_arcTrack, s_arcIndicator;

// Sanftes Press-Feedback (wie UIKit-Highlight)
static const lv_style_prop_t TRANS_PROPS[] = {LV_STYLE_BG_COLOR, LV_STYLE_OPA, LV_STYLE_PROP_INV};
static lv_style_transition_dsc_t s_trans;

const Palette& colors() { return s_dark ? DARK : LIGHT; }
bool isDark() { return s_dark; }

// Einmalig: farbunabhängige Eigenschaften
static void initStyles() {
    for (lv_style_t* s : {&s_screen, &s_card, &s_cardPressed, &s_button, &s_buttonAccent, &s_pressed,
                          &s_textTimeXL, &s_textTime, &s_textLarge, &s_textTitle, &s_textHeadline,
                          &s_textBody, &s_textCaption, &s_textAccent, &s_textAccentAlt, &s_lineText,
                          &s_lineSecondary, &s_lineAccent, &s_fillAccentAlt, &s_fillBg, &s_arcTrack,
                          &s_arcIndicator}) {
        lv_style_init(s);
    }
    lv_style_transition_dsc_init(&s_trans, TRANS_PROPS, lv_anim_path_ease_out, 120, 0, nullptr);

    lv_style_set_bg_opa(&s_screen, LV_OPA_COVER);
    lv_style_set_border_width(&s_screen, 0);
    lv_style_set_radius(&s_screen, 0);
    lv_style_set_pad_all(&s_screen, 0);
    lv_style_set_text_font(&s_screen, &font_body);

    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, RADIUS_CARD);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, PAD);
    lv_style_set_pad_row(&s_card, 4);
    lv_style_set_transition(&s_card, &s_trans);

    for (lv_style_t* s : {&s_button, &s_buttonAccent}) {
        lv_style_set_bg_opa(s, LV_OPA_COVER);
        lv_style_set_radius(s, RADIUS_PILL);
        lv_style_set_border_width(s, 0);
        lv_style_set_shadow_width(s, 0);
        lv_style_set_pad_hor(s, 16);
        lv_style_set_pad_ver(s, 12);
        lv_style_set_text_font(s, &font_headline);
        lv_style_set_transition(s, &s_trans);
    }
    lv_style_set_text_color(&s_buttonAccent, lv_color_white());

    lv_style_set_opa(&s_pressed, LV_OPA_60);

    lv_style_set_text_font(&s_textTimeXL, &font_time_xl);
    lv_style_set_text_letter_space(&s_textTimeXL, -2);
    lv_style_set_text_font(&s_textTime, &font_time);
    lv_style_set_text_letter_space(&s_textTime, -2);
    lv_style_set_text_font(&s_textLarge, &font_large);
    lv_style_set_text_font(&s_textTitle, &font_title);
    lv_style_set_text_font(&s_textHeadline, &font_headline);
    lv_style_set_text_font(&s_textBody, &font_body);
    lv_style_set_text_font(&s_textCaption, &font_caption);

    lv_style_set_line_rounded(&s_lineText, true);
    lv_style_set_line_width(&s_lineText, 5);
    lv_style_set_line_rounded(&s_lineSecondary, true);
    lv_style_set_line_width(&s_lineSecondary, 2);
    lv_style_set_line_rounded(&s_lineAccent, true);
    lv_style_set_line_width(&s_lineAccent, 2);

    for (lv_style_t* s : {&s_fillAccentAlt, &s_fillBg}) {
        lv_style_set_bg_opa(s, LV_OPA_COVER);
        lv_style_set_radius(s, LV_RADIUS_CIRCLE);
        lv_style_set_border_width(s, 0);
    }

    lv_style_set_arc_width(&s_arcTrack, 6);
    lv_style_set_arc_rounded(&s_arcTrack, true);
    lv_style_set_bg_opa(&s_arcTrack, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_arcTrack, 0);
    lv_style_set_pad_all(&s_arcTrack, 0);
    lv_style_set_arc_width(&s_arcIndicator, 6);
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

    for (lv_style_t* s : {&s_textTimeXL, &s_textTime, &s_textLarge, &s_textTitle, &s_textHeadline, &s_textBody})
        lv_style_set_text_color(s, p.text);
    lv_style_set_text_color(&s_textCaption, p.textSecondary);
    lv_style_set_text_color(&s_textAccent, p.accent);
    lv_style_set_text_color(&s_textAccentAlt, p.accentAlt);

    lv_style_set_line_color(&s_lineText, p.text);
    lv_style_set_line_color(&s_lineSecondary, p.textSecondary);
    lv_style_set_line_color(&s_lineAccent, p.accentAlt);

    lv_style_set_bg_color(&s_fillAccentAlt, p.accentAlt);
    lv_style_set_bg_color(&s_fillBg, p.bg);
    lv_style_set_arc_color(&s_arcTrack, p.surfaceAlt);
    lv_style_set_arc_color(&s_arcIndicator, p.accentAlt);

    // LVGL-Standardtheme für eingebaute Widgets (Switch, Liste, Msgbox ...)
    // Primärfarbe Grün -> Schalter sehen aus wie iOS/watchOS-Toggles
    lv_theme_t* th = lv_theme_default_init(s_disp, p.green, p.red, s_dark, &font_body);
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
lv_style_t* textTimeXL()    { return &s_textTimeXL; }
lv_style_t* textTime()      { return &s_textTime; }
lv_style_t* textLarge()     { return &s_textLarge; }
lv_style_t* textTitle()     { return &s_textTitle; }
lv_style_t* textHeadline()  { return &s_textHeadline; }
lv_style_t* textBody()      { return &s_textBody; }
lv_style_t* textCaption()   { return &s_textCaption; }
lv_style_t* textAccent()    { return &s_textAccent; }
lv_style_t* textAccentAlt() { return &s_textAccentAlt; }
lv_style_t* lineText()      { return &s_lineText; }
lv_style_t* lineSecondary() { return &s_lineSecondary; }
lv_style_t* lineAccent()    { return &s_lineAccent; }
lv_style_t* fillAccentAlt() { return &s_fillAccentAlt; }
lv_style_t* fillBg()        { return &s_fillBg; }
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

static lv_obj_t* glyphPlate(lv_obj_t* parent, const char* symbol, lv_color_t color, lv_coord_t size,
                            lv_coord_t radius, const lv_font_t* font, bool darkGlyph) {
    lv_obj_t* plate = lv_obj_create(parent);
    lv_obj_remove_style_all(plate);
    lv_obj_set_size(plate, size, size);
    lv_obj_set_style_radius(plate, radius, 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(plate, color, 0);
    lv_obj_clear_flag(plate, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* g = lv_label_create(plate);
    lv_label_set_text(g, symbol);
    lv_obj_set_style_text_font(g, font, 0);
    lv_obj_set_style_text_color(g, darkGlyph ? lv_color_black() : lv_color_white(), 0);
    lv_obj_center(g);
    return plate;
}

lv_obj_t* appIcon(lv_obj_t* parent, const char* symbol, lv_color_t color, lv_coord_t size, bool darkGlyph) {
    return glyphPlate(parent, symbol, color, size, LV_RADIUS_CIRCLE, &font_icon, darkGlyph);
}

lv_obj_t* rowIcon(lv_obj_t* parent, const char* symbol, lv_color_t color) {
    return glyphPlate(parent, symbol, color, 30, RADIUS_ICON, &font_caption, false);
}

lv_obj_t* pillButton(lv_obj_t* parent, const char* text, bool accent) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, accent ? &s_buttonAccent : &s_button, 0);
    lv_obj_add_style(b, &s_pressed, LV_STATE_PRESSED);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

}  // namespace theme
