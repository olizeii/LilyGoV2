#pragma once
// =============================================================================
//  Hardware-Belegung LilyGo T-Watch-S3
//  Quelle: LilyGoLib / T-Watch-S3 Schaltplan
// =============================================================================

// --- Display ST7789V (SPI) ---------------------------------------------------
#define PIN_TFT_MOSI      13
#define PIN_TFT_SCLK      18
#define PIN_TFT_CS        12
#define PIN_TFT_DC        38
#define PIN_TFT_RST       -1
#define PIN_TFT_BL        45
#define TFT_WIDTH         240
#define TFT_HEIGHT        240
// Falls das Bild auf dem Kopf steht: 0 <-> 2 tauschen.
#define TFT_ROTATION      2

// --- Touch FT6336U (eigener I2C-Bus) -----------------------------------------
#define PIN_TOUCH_SDA     39
#define PIN_TOUCH_SCL     40
#define PIN_TOUCH_INT     16
#define TOUCH_I2C_ADDR    0x38
// Der FT6336 liefert Koordinaten bereits in der Ausrichtung von TFT_ROTATION 2.
// LovyanGFX dreht den Touch aber zusätzlich mit der Display-Rotation mit ->
// ohne Ausgleich wäre jede Geste in X und Y gespiegelt. Offset 2 hebt das auf:
// ((2 + 2) & 3) = 0. Bei TFT_ROTATION 0 hier ebenfalls 0 eintragen.
// (Werte 4..7 = zusätzlich gespiegelt, siehe Panel_Device::convertRawXY)
#define TOUCH_OFFSET_ROTATION 2

// --- Sensor-I2C-Bus (PMU, RTC, BMA423, DRV2605) ------------------------------
#define PIN_I2C_SDA       10
#define PIN_I2C_SCL       11

// --- Interrupts --------------------------------------------------------------
#define PIN_PMU_INT       21
#define PIN_RTC_INT       17
#define PIN_SENSOR_INT    14

// --- IR-Sender ---------------------------------------------------------------
#define PIN_IR_TX         2

// --- Audio (PDM-Mikro + MAX98357 I2S) ----------------------------------------
#define PIN_MIC_DATA      47
#define PIN_MIC_CLK       44
#define PIN_I2S_BCK       48
#define PIN_I2S_WS        15
#define PIN_I2S_DOUT      46

// --- LoRa SX1262 (optional) --------------------------------------------------
#define PIN_RADIO_SCK     3
#define PIN_RADIO_MISO    4
#define PIN_RADIO_MOSI    1
#define PIN_RADIO_CS      5
#define PIN_RADIO_RST     8
#define PIN_RADIO_BUSY    7
#define PIN_RADIO_DIO1    9
