#pragma once
#include <Arduino.h>

// =============================================================================
//  Karte + Navigation (Vollbild auf lv_layer_top).
//
//  Die Karte rendert das Handy (WatchBridge, CARTO-Kacheln) als 240x240-Bild mit
//  16 Farben und schickt es per MAP_BEGIN/MAP/MAP_END. Die Uhr fordert mit
//  MAP_REQ|<zoom>|<dark> an, solange der Bildschirm offen ist (alle 20 s =
//  "Lease"; das Handy schickt in dieser Zeit neue Bilder, wenn du dich bewegst).
// =============================================================================
namespace mapscreen {

void begin();   // einmalig: Abos auf Map/Nav/Location
void open();
void close();
bool isOpen();

// Symbol (FontAwesome) für ein Navigations-Manöver
const char* maneuverIcon(const String& maneuver);

}  // namespace mapscreen
