#pragma once
#include <lvgl.h>

#include <functional>

// =============================================================================
//  Theme – Apple-Style Light/Dark Mode
//
//  Apps benutzen AUSSCHLIESSLICH diese Styles (statt lokaler Farben). Beim
//  Umschalten werden die Style-Objekte in-place geändert und LVGL zeichnet alle
//  Widgets neu -> kein App-Code nötig. Für Sonderfälle gibt es onChange().
// =============================================================================
namespace theme {

struct Palette {
    lv_color_t bg;            // Hintergrund (Dark: reines Schwarz -> OLED-Look)
    lv_color_t surface;       // Karten
    lv_color_t surfaceAlt;    // Buttons auf Karten / gedrückt
    lv_color_t text;
    lv_color_t textSecondary;
    lv_color_t separator;
    lv_color_t accent;        // iOS Blau
    lv_color_t accentAlt;     // iOS Orange (Watchface-Akzente)
    lv_color_t red;
    lv_color_t green;
};

// Abstände / Radien (8er-Raster)
constexpr lv_coord_t PAD         = 12;
constexpr lv_coord_t RADIUS_CARD = 18;
constexpr lv_coord_t RADIUS_PILL = LV_RADIUS_CIRCLE;

void begin(lv_disp_t* disp, bool dark);

bool isDark();
void setDark(bool dark);  // aktualisiert alle Styles + persistiert
void toggle();
const Palette& colors();

void onChange(std::function<void()> listener);

// --- Geteilte Styles ---------------------------------------------------------
lv_style_t* screen();        // Tile-/Seitenhintergrund
lv_style_t* card();          // abgerundete Fläche
lv_style_t* cardPressed();
lv_style_t* button();        // Pill-Button (surfaceAlt)
lv_style_t* buttonAccent();  // Pill-Button in Akzentfarbe
lv_style_t* pressed();       // Press-Feedback (leicht abgedunkelt)

lv_style_t* textLarge();     // 48 px – Uhrzeit
lv_style_t* textTitle();     // 24 px
lv_style_t* textBody();      // 16 px
lv_style_t* textCaption();   // 14 px, sekundär
lv_style_t* textAccent();    // Akzentfarbe (mit Font-Style kombinierbar)
lv_style_t* textAccentAlt();

lv_style_t* lineText();      // Linien in Textfarbe (Zeiger)
lv_style_t* lineSecondary(); // Linien sekundär (Ticks)
lv_style_t* lineAccent();    // Linien Akzent (Sekundenzeiger)

lv_style_t* fillAccentAlt(); // runde Fläche in Orange (Zeigernabe, Punkte)
lv_style_t* arcTrack();      // lv_arc MAIN: dezente Spur
lv_style_t* arcIndicator();  // lv_arc INDICATOR: Akzent

// --- Helfer ------------------------------------------------------------------
// Label mit Theme-Style anlegen
lv_obj_t* label(lv_obj_t* parent, lv_style_t* style, const char* text = "");
// Abgerundete Karte anlegen. Nicht klickbar – für Buttons-Karten
// LV_OBJ_FLAG_CLICKABLE wieder setzen (Press-Style ist schon hinterlegt).
lv_obj_t* cardObj(lv_obj_t* parent);

}  // namespace theme
