#pragma once

// =============================================================================
//  "Ausschalten?" – erscheint, wenn die Seitentaste 1,5 s gehalten wird.
//  Roter Knopf = Uhr komplett aus (AXP2101 aus, praktisch 0 mA).
//  Wieder einschalten: Seitentaste 1 s halten.
// =============================================================================
namespace powerscreen {

void open();
void close();
bool isOpen();

}  // namespace powerscreen
