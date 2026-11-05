#pragma once
#include <Arduino.h>

// =============================================================================
//  Voice – nimmt über das PDM-Mikrofon (GPIO 47/44) auf und streamt die
//  Sprache live per BLE an WatchBridge (Spracherkennung läuft auf dem Handy).
//
//  16 kHz Mono, IMA-ADPCM (4 Bit -> 8 KB/s), Base64 in "AUD|..."-Notifies.
//  Jeder Block trägt seinen eigenen ADPCM-Startzustand:
//    [seq u8][predictor i16 LE][step-index u8][Nibbles, low zuerst ...]
//  -> geht ein Block verloren, decodiert der Rest trotzdem sauber.
//
//  Läuft in einem eigenen FreeRTOS-Task (Core 0), LVGL bleibt unberührt.
// =============================================================================
namespace voice {

constexpr uint32_t SAMPLE_RATE = 16000;

// Startet Aufnahme + Streaming. false = BLE-MTU zu klein / I2S-Fehler.
bool start(uint32_t maxMs = 12000);
void stop();             // beendet nach dem aktuellen Block (nicht blockierend)
bool isRunning();
bool timedOut();         // maxMs erreicht -> von selbst gestoppt

// Geglätteter Pegel 0..1 (für die Animation)
float level();

// Direkter Mikrofon-Zugriff (z.B. Studio-Aufnahme). Nicht gleichzeitig mit start() nutzbar.
bool micOpen();
size_t micRead(int16_t* buf, size_t samples, uint32_t timeoutMs);  // Anzahl gelesener Samples
void micClose();

}  // namespace voice
