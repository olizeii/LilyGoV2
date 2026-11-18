#pragma once
#include <Arduino.h>

// =============================================================================
//  Soundeffekte über den Lautsprecher (MAX98357, I2S1) – synthetisch erzeugt,
//  keine Audiodateien. open() hält den Verstärker bereit (kurze Latenz),
//  close() schaltet alles wieder ab. Nicht gleichzeitig mit dem Studio nutzen.
// =============================================================================
namespace sfx {

enum class Sound : uint8_t { Whip, Gun };

bool open();
void close();
bool isOpen();
void play(Sound s);   // nicht blockierend; ein laufender Sound wird ersetzt

}  // namespace sfx
