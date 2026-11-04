#pragma once
#include <Arduino.h>
#include <lvgl.h>

// =============================================================================
//  KI-Assistent (Vollbild auf lv_layer_top) – lange auf ein Watchface drücken.
//
//  Ablauf:  Kugel pulsiert mit dem Mikrofonpegel  ->  Handy erkennt die Sprache
//           ->  "Einen Moment …"  ->  Antwort erscheint Wort für Wort als Text.
//
//  Uhr -> Handy:  AI_START|16000   AUD|<b64>…   AUD_END   AI_CANCEL
//  Handy -> Uhr:  AI_STATE|listen|think   AI_HEARD|<text>   AI_TEXT|<stück>
//                 AI_DONE   AI_ERR|<meldung>
//                 AI_ACT|dark|0/1   AI_ACT|bright|<10-100>   AI_ACT|ir|<taste>|<n>
//                 AI_ACT|open|<app>   AI_ACT|timer|<sek>|<titel>
//
//  Tippen auf die Kugel = fertig gesprochen. Seitentaste / ✕ = schließen.
// =============================================================================
namespace aiscreen {

void begin();                  // einmalig (Topic-Handler registrieren)
void attach(lv_obj_t* tile);   // Langes Drücken auf dieser Kachel öffnet den Assistenten

void open();
void close();
bool isOpen();

}  // namespace aiscreen
