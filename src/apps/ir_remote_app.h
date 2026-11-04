#pragma once
#include "ui/app.h"

// =============================================================================
//  TV-Fernbedienung über die IR-LED (GPIO 2) mit IRremoteESP8266.
//
//  Marken mit fertigen Codes: Samsung, LG, Sony, Philips (RC6), Panasonic
//  und Optoma-Beamer (NEC 32CD, rechte Wippe = Menü hoch/runter).
//  Auswahl über den Marken-Knopf oben links, gespeichert im NVS ("ir_brand").
//  Lautstärke/Kanal wiederholen beim Gedrückthalten.
// =============================================================================
class IrRemoteApp : public WatchApp {
public:
    enum Key : uint8_t { K_POWER, K_VOL_UP, K_VOL_DOWN, K_MUTE, K_CH_UP, K_CH_DOWN, K_INPUT, K_KEY_COUNT };

    const char* name() const override { return "Fernbedienung"; }
    void create(lv_obj_t* tile) override;

    // Taste per Name senden (KI-Assistent): power, vol_up, vol_down, mute, ch_up, ch_down, input
    static bool remote(const char* key, uint8_t times = 1);

private:
    void send(Key key);
    void openBrandPicker();
    void updateBrandLabel();
    lv_obj_t* makeKey(lv_obj_t* parent, Key key, const char* symbol, lv_coord_t w, lv_coord_t h,
                      lv_color_t bg, lv_color_t fg, bool repeat);
    lv_obj_t* makeRocker(lv_obj_t* tile, lv_coord_t x, const char* caption, Key up, Key down);

    lv_obj_t* m_brandLabel = nullptr;
    lv_obj_t* m_chCaption  = nullptr;
    uint8_t m_brand        = 0;
    bool m_rc6Toggle       = false;
};
