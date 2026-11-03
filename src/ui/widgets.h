#pragma once
#include <lvgl.h>

#include <functional>

#include "ui/theme.h"

// =============================================================================
//  Wiederverwendbare Bausteine im watchOS-Stil.
// =============================================================================
namespace ui {

#define UI_NNBSP "\xE2\x80\xAF"  // schmales geschütztes Leerzeichen ("82 %")
#define UI_DASH  "\xE2\x80\x93"  // Halbgeviertstrich

// Unsichtbarer Layout-Container (keine Klicks, kein Scrollen)
lv_obj_t* box(lv_obj_t* parent);

// Text nur setzen, wenn er sich ändert (spart Neuzeichnen)
void setText(lv_obj_t* label, const char* text);

// Runder Knopf mit Glyph. bg/fg frei wählbar, Press-Feedback inklusive.
lv_obj_t* roundButton(lv_obj_t* parent, const char* symbol, lv_coord_t size, lv_color_t bg, lv_color_t fg,
                      const lv_font_t* font);

// Leerer Zustand: farbiges Icon, Titel, Untertitel – zentriert in parent.
// Gibt den Container zurück (zum Ein-/Ausblenden).
lv_obj_t* emptyState(lv_obj_t* parent, const char* icon, lv_color_t tint, const char* title,
                     const char* subtitle);

// Vollbild-Sheet auf lv_layer_top mit ×-Knopf oben links und Titel.
// `content` ist ein scrollbarer Spalten-Container für den Inhalt.
struct Sheet {
    lv_obj_t* root    = nullptr;
    lv_obj_t* header  = nullptr;
    lv_obj_t* title   = nullptr;
    lv_obj_t* content = nullptr;
};
Sheet openSheet(const char* title, std::function<void()> onClose = nullptr);
void closeSheet(lv_obj_t* root);   // sicher aus Event-Callbacks heraus
void closeAllSheets();             // z.B. beim Aufwachen
bool sheetOpen();

}  // namespace ui
