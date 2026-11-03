#pragma once
#include <lvgl.h>

#include <functional>

#include "ui/icons.h"

// =============================================================================
//  Theme – Apple-Style Light/Dark Mode
//
//  Apps benutzen AUSSCHLIESSLICH diese Styles (statt lokaler Farben). Beim
//  Umschalten werden die Style-Objekte in-place geändert und LVGL zeichnet alle
//  Widgets neu -> kein App-Code nötig. Für Sonderfälle gibt es onChange().
//
//  Typografie (Inter ≈ SF Pro, mit Umlauten):
//    caption 15 · body 18 · headline 18 semibold · title 24 semibold
//    large 34 (Zahlen/Werte) · time 76 · timeXL 88 (nur Ziffern)
// =============================================================================

LV_FONT_DECLARE(font_caption);
LV_FONT_DECLARE(font_body);
LV_FONT_DECLARE(font_headline);
LV_FONT_DECLARE(font_title);
LV_FONT_DECLARE(font_large);
LV_FONT_DECLARE(font_time);
LV_FONT_DECLARE(font_time_xl);
LV_FONT_DECLARE(font_icon);

namespace theme {

struct Palette {
    lv_color_t bg;            // Hintergrund (Dark: reines Schwarz -> OLED-Look)
    lv_color_t surface;       // Karten / Plattformen
    lv_color_t surfaceAlt;    // gedrückt, Spuren, Buttons auf Karten
    lv_color_t text;
    lv_color_t textSecondary;
    lv_color_t separator;
    lv_color_t accent;        // System-Blau (Bedienelemente)
    lv_color_t accentAlt;     // System-Orange (Watchface-Akzente)
    lv_color_t red;
    lv_color_t green;
};

// Apple-Systemfarben (watchOS/iOS Dark-Varianten) für App-Icons & Akzente.
// Bleiben in beiden Modi gleich, damit Icons wiedererkennbar sind.
enum class Tint : uint8_t { Red, Orange, Yellow, Green, Mint, Teal, Cyan, Blue, Indigo, Purple, Pink, Gray, Spotify };
lv_color_t tint(Tint t);

// Abstände / Radien (4er-Raster)
constexpr lv_coord_t PAD         = 12;   // Bildschirmrand
constexpr lv_coord_t GAP         = 8;    // zwischen Elementen
constexpr lv_coord_t ROW_H       = 50;   // Listenzeile (>= 44 px Touch-Ziel)
constexpr lv_coord_t RADIUS_CARD = 20;
constexpr lv_coord_t RADIUS_ICON = 8;
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

lv_style_t* textTimeXL();    // 88 px – gestapelte Ziffern
lv_style_t* textTime();      // 76 px – Uhrzeit
lv_style_t* textLarge();     // 34 px – große Werte
lv_style_t* textTitle();     // 24 px semibold
lv_style_t* textHeadline();  // 18 px semibold
lv_style_t* textBody();      // 18 px
lv_style_t* textCaption();   // 15 px, sekundär
lv_style_t* textAccent();    // Akzentfarbe (mit Font-Style kombinierbar)
lv_style_t* textAccentAlt();

lv_style_t* lineText();      // Linien in Textfarbe (Zeiger)
lv_style_t* lineSecondary(); // Linien sekundär (Ticks)
lv_style_t* lineAccent();    // Linien Akzent (Sekundenzeiger)

lv_style_t* fillAccentAlt(); // runde Fläche in Orange (Zeigernabe, Punkte)
lv_style_t* fillBg();        // runde Fläche in Hintergrundfarbe
lv_style_t* arcTrack();      // lv_arc MAIN: dezente Spur
lv_style_t* arcIndicator();  // lv_arc INDICATOR: Akzent

// --- Helfer ------------------------------------------------------------------
// Label mit Theme-Style anlegen
lv_obj_t* label(lv_obj_t* parent, lv_style_t* style, const char* text = "");
// Abgerundete Karte anlegen. Nicht klickbar – für Buttons-Karten
// LV_OBJ_FLAG_CLICKABLE wieder setzen (Press-Style ist schon hinterlegt).
lv_obj_t* cardObj(lv_obj_t* parent);
// Runder App-Icon-Kreis (watchOS-Stil) mit weißem Glyph.
lv_obj_t* appIcon(lv_obj_t* parent, const char* symbol, lv_color_t color, lv_coord_t size,
                  bool darkGlyph = false);
// Kleines abgerundetes Quadrat mit Glyph für Listenzeilen (iOS-Einstellungen).
lv_obj_t* rowIcon(lv_obj_t* parent, const char* symbol, lv_color_t color);
// Pill-Button mit Text
lv_obj_t* pillButton(lv_obj_t* parent, const char* text, bool accent);

}  // namespace theme
