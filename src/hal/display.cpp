#include "display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

#include "config/pins.h"

namespace display {

// --- LovyanGFX-Konfiguration -------------------------------------------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 m_panel;
    lgfx::Bus_SPI      m_bus;
    lgfx::Light_PWM    m_light;
    lgfx::Touch_FT5x06 m_touch;  // FT6336U ist FT5x06-kompatibel

public:
    LGFX() {
        {
            auto cfg        = m_bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_TFT_SCLK;
            cfg.pin_mosi    = PIN_TFT_MOSI;
            cfg.pin_miso    = -1;
            cfg.pin_dc      = PIN_TFT_DC;
            m_bus.config(cfg);
            m_panel.setBus(&m_bus);
        }
        {
            auto cfg             = m_panel.config();
            cfg.pin_cs           = PIN_TFT_CS;
            cfg.pin_rst          = PIN_TFT_RST;
            cfg.pin_busy         = -1;
            cfg.memory_width     = 240;
            cfg.memory_height    = 320;  // ST7789 RAM ist 240x320 -> Offset bei Rotation 2
            cfg.panel_width      = TFT_WIDTH;
            cfg.panel_height     = TFT_HEIGHT;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = false;
            cfg.invert           = true;   // IPS-Panel
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            m_panel.config(cfg);
        }
        {
            auto cfg        = m_light.config();
            cfg.pin_bl      = PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            m_light.config(cfg);
            m_panel.setLight(&m_light);
        }
        {
            auto cfg            = m_touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = TFT_WIDTH - 1;
            cfg.y_min           = 0;
            cfg.y_max           = TFT_HEIGHT - 1;
            cfg.pin_int         = PIN_TOUCH_INT;
            cfg.bus_shared      = false;
            cfg.offset_rotation = TOUCH_OFFSET_ROTATION;
            cfg.i2c_port        = 1;      // I2C0 gehört Wire (PMU/RTC)
            cfg.i2c_addr        = TOUCH_I2C_ADDR;
            cfg.pin_sda         = PIN_TOUCH_SDA;
            cfg.pin_scl         = PIN_TOUCH_SCL;
            cfg.freq            = 400000;
            m_touch.config(cfg);
            m_panel.setTouch(&m_touch);
        }
        setPanel(&m_panel);
    }
};

static LGFX s_tft;
static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;
static lv_indev_drv_t s_indevDrv;

static constexpr uint32_t BUF_LINES = 40;

static uint8_t s_brightness   = 160;
static bool s_asleep          = false;
static bool s_touchWake       = false;
static bool s_swallowTouch    = false;  // bis zum Loslassen ignorieren

// --- LVGL-Callbacks ----------------------------------------------------------
static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
    if (!s_asleep) {
        uint32_t w = lv_area_get_width(area);
        uint32_t h = lv_area_get_height(area);
        s_tft.startWrite();
        s_tft.setAddrWindow(area->x1, area->y1, w, h);
        // LV_COLOR_16_SWAP=1 -> Daten liegen bereits Big-Endian vor
        s_tft.writePixels(reinterpret_cast<lgfx::swap565_t*>(&px->full), w * h);
        s_tft.endWrite();
    }
    lv_disp_flush_ready(drv);
}

static void touchReadCb(lv_indev_drv_t*, lv_indev_data_t* data) {
    uint16_t x, y;
    bool touched = s_tft.getTouch(&x, &y);

    if (s_asleep) {
        if (touched) {
            s_touchWake   = true;
            s_swallowTouch = true;
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (s_swallowTouch) {
        if (!touched) s_swallowTouch = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (touched) {
        data->point.x = x;
        data->point.y = y;
    }
}

// --- API ---------------------------------------------------------------------
lv_disp_t* begin(uint8_t brightness) {
    s_tft.init();
    s_tft.setRotation(TFT_ROTATION);
    s_tft.fillScreen(TFT_BLACK);
    setBrightness(brightness);

    lv_init();

    // Zwei Teil-Puffer im internen RAM (schneller als PSRAM)
    size_t bytes = TFT_WIDTH * BUF_LINES * sizeof(lv_color_t);
    auto* buf1 = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    auto* buf2 = static_cast<lv_color_t*>(heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    lv_disp_draw_buf_init(&s_drawBuf, buf1, buf2, TFT_WIDTH * BUF_LINES);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res  = TFT_WIDTH;
    s_dispDrv.ver_res  = TFT_HEIGHT;
    s_dispDrv.flush_cb = flushCb;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_t* disp = lv_disp_drv_register(&s_dispDrv);

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type    = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = touchReadCb;
    lv_indev_drv_register(&s_indevDrv);

    return disp;
}

void setBrightness(uint8_t level) {
    s_brightness = level;
    if (!s_asleep) s_tft.setBrightness(level);
}

void sleep() {
    if (s_asleep) return;
    s_asleep = true;
    s_tft.setBrightness(0);
    s_tft.sleep();
}

void wake() {
    if (!s_asleep) return;
    s_tft.wakeup();
    s_asleep = false;
    lv_obj_invalidate(lv_scr_act());  // im Sleep wurde nichts geflusht
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);             // erst zeichnen, dann Licht an -> kein Flackern
    s_tft.setBrightness(s_brightness);
    lv_disp_trig_activity(nullptr);
}

bool isAsleep() { return s_asleep; }

bool consumeTouchWake() {
    bool w = s_touchWake;
    s_touchWake = false;
    return w;
}

}  // namespace display
