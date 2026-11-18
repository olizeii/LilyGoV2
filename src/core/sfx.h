#pragma once
#include <Arduino.h>

// =============================================================================
//  Soundeffekte über den Lautsprecher (MAX98357, I2S1) – synthetisch erzeugt,
//  keine Audiodateien. open() hält den Verstärker bereit (kurze Latenz),
//  close() schaltet alles wieder ab. Nicht gleichzeitig mit dem Studio nutzen.
// =============================================================================
namespace sfx {

// Die ersten drei lassen sich per Handy-App durch eigene Sounds ersetzen.
enum class Sound : uint8_t { Whip, Gun, Burst, SaberOn, SaberOff, SaberSwing, SaberClash, COUNT };

void begin();   // registriert den Empfang eigener Sounds vom Handy (SFX_BEGIN/SFX/SFX_END/SFX_RESET)

bool open();
void close();
bool isOpen();
void play(Sound s);   // nicht blockierend; ein laufender Sound wird ersetzt

// Lichtschwert: Dauer-Summen, live gemischt; Bewegung (0..~1.2) macht es lauter/höher
void setHum(bool on);
void setMotion(float m);

// Dauerfeuer-Einstellungen (vom Handy, GEST_CFG)
uint8_t burstHz();
float burstThreshold();

}  // namespace sfx
