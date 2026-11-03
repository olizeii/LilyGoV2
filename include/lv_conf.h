/**
 * LVGL 8.3 Konfiguration für die T-Watch-S3.
 * Alles, was hier nicht definiert ist, bekommt den Default aus lv_conf_internal.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   FARBEN
 *====================*/
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1      /* SPI-Display erwartet Big-Endian RGB565 */
#define LV_COLOR_SCREEN_TRANSP 0

/*====================
   SPEICHER
 *====================*/
/* stdlib malloc: Arduino-ESP32 legt große Blöcke automatisch ins PSRAM */
#define LV_MEM_CUSTOM      1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

/*====================
   HAL
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD  16     /* ~60 fps Ziel */
#define LV_INDEV_DEF_READ_PERIOD 16
#define LV_DPI_DEF               200

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*====================
   FEATURES
 *====================*/
#define LV_USE_LOG      0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#define LV_SHADOW_CACHE_SIZE 0
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/*====================
   SCHRIFTEN (Montserrat = klare Sans-Serif, kommt dem SF-Look am nächsten)
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_TXT_ENC LV_TXT_ENC_UTF8

/*====================
   WIDGETS
 *====================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1

#define LV_USE_LIST       1
#define LV_USE_MSGBOX     1
#define LV_USE_TILEVIEW   1
#define LV_USE_METER      1
#define LV_USE_SPINNER    1

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

#endif /* LV_CONF_H */
