#pragma once
#include <Arduino.h>

// =============================================================================
//  Studio-Engine: ein Beat (vom Handy) + bis zu 3 Gesangsspuren (Mikrofon).
//  Alles 16 kHz mono, intern als µ-law (1 Byte/Sample) im PSRAM, gesichert im
//  LittleFS. Abspielen über den MAX98357 (I2S1), Aufnehmen über das PDM-Mikro
//  (I2S0) – beides gleichzeitig: beim Aufnehmen läuft der Beat mit.
//
//  Protokoll vom Handy:
//    BEAT_BEGIN|<name>|<bytes>   BEAT|<base64 µ-law>   BEAT_END
//  Uhr antwortet alle WINDOW Pakete mit BEAT_ACK|<n> (Flusskontrolle).
// =============================================================================
namespace studio {

constexpr uint32_t RATE       = 16000;
constexpr uint8_t  MAX_CLIPS  = 3;
constexpr uint32_t MAX_BEAT_S = 40;
constexpr uint32_t MAX_CLIP_S = 20;

struct Clip {
    bool used        = false;
    uint32_t offset  = 0;   // Start im Song (Samples)
    uint32_t length  = 0;   // Samples
};

void begin();     // lädt Beat/Spuren aus dem Flash, registriert den BEAT-Handler

// Zustand
bool hasBeat();
const String& beatName();
uint32_t beatLength();               // Samples
const Clip& clip(uint8_t i);
uint32_t songLength();               // max(Beat, Ende der letzten Spur)
uint32_t position();                 // aktuelle Abspielposition (Samples)
bool isPlaying();
bool isRecording();
int recordingClip();                 // Index der Spur, in die gerade aufgenommen wird, sonst -1
float level();                       // Pegel 0..1 (Aufnahme bzw. Ausgabe)
int transferPercent();               // -1 = keine Übertragung

// Steuerung
void play(uint32_t from);            // spielt ab `from` bis Songende
bool record(uint32_t from);          // nimmt in die erste freie Spur auf (false = keine frei / Mikro belegt)
void stop();
void moveClip(uint8_t i, uint32_t newOffset);   // nur im Stillstand
void deleteClip(uint8_t i);
void setBeatVolume(uint8_t pct);     // 0..100
uint8_t beatVolume();

// Änderungszähler – UI baut neu, wenn er sich ändert
uint32_t revision();

}  // namespace studio
