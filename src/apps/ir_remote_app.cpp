#include "ir_remote_app.h"

#include <IRremoteESP8266.h>
#include <IRsend.h>

#include "config/pins.h"
#include "core/settings.h"
#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

constexpr const char* KEY_BRAND = "ir_brand";

enum class Proto : uint8_t { Samsung, NEC, Sony, RC6, Panasonic };

struct Brand {
    const char* name;
    Proto proto;
    uint32_t codes[IrRemoteApp::K_KEY_COUNT];  // K_POWER, VOL+, VOL-, K_MUTE, CH+, CH-, K_INPUT
};

// Verbreitete Standardcodes der jeweiligen Hersteller-Fernbedienungen
const Brand BRANDS[] = {
    {"Samsung", Proto::Samsung,
     {0xE0E040BF, 0xE0E0E01F, 0xE0E0D02F, 0xE0E0F00F, 0xE0E048B7, 0xE0E008F7, 0xE0E0807F}},
    {"LG", Proto::NEC,
     {0x20DF10EF, 0x20DF40BF, 0x20DFC03F, 0x20DF906F, 0x20DF00FF, 0x20DF807F, 0x20DFD02F}},
    {"Sony", Proto::Sony, {0xA90, 0x490, 0xC90, 0x290, 0x090, 0x890, 0xA50}},
    {"Philips", Proto::RC6, {0x0C, 0x10, 0x11, 0x0D, 0x4C, 0x4D, 0x38}},
    {"Panasonic", Proto::Panasonic,
     {0x100BCBD, 0x1000405, 0x1008485, 0x1004C4D, 0x1002C2D, 0x100ACAD, 0x100A0A1}},
};
constexpr uint8_t BRAND_COUNT = sizeof(BRANDS) / sizeof(BRANDS[0]);

IRsend s_ir(PIN_IR_TX);

struct KeyCtx {
    IrRemoteApp* app;
    IrRemoteApp::Key key;
};
KeyCtx s_ctx[IrRemoteApp::K_KEY_COUNT];

}  // namespace

// -----------------------------------------------------------------------------
void IrRemoteApp::create(lv_obj_t* tile) {
    s_ir.begin();
    m_brand = settings::getU8(KEY_BRAND, 0);
    if (m_brand >= BRAND_COUNT) m_brand = 0;
    const theme::Palette& p = theme::colors();

    // --- Kopfzeile: Marke (antippen = wechseln) + Power ---
    lv_obj_t* brand = theme::pillButton(tile, "", false);
    lv_obj_set_height(brand, 40);
    lv_obj_set_style_pad_ver(brand, 0, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_LEFT, 14, 12);
    m_brandLabel = lv_obj_get_child(brand, 0);
    lv_obj_add_event_cb(
        brand, [](lv_event_t* e) { static_cast<IrRemoteApp*>(lv_event_get_user_data(e))->openBrandPicker(); },
        LV_EVENT_CLICKED, this);
    updateBrandLabel();

    lv_obj_t* power = makeKey(tile, K_POWER, LV_SYMBOL_POWER, 48, 48, p.red, lv_color_white(), false);
    lv_obj_align(power, LV_ALIGN_TOP_RIGHT, -14, 8);

    // --- Wippen links (Lautstärke) und rechts (Kanal) ---
    makeRocker(tile, 16, "VOL", K_VOL_UP, K_VOL_DOWN);
    makeRocker(tile, 240 - 16 - 62, "CH", K_CH_UP, K_CH_DOWN);

    // --- Mitte: Stumm + Eingang ---
    lv_obj_t* mute = makeKey(tile, K_MUTE, LV_SYMBOL_MUTE, 56, 56, p.surfaceAlt, p.text, false);
    lv_obj_align(mute, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_t* input = makeKey(tile, K_INPUT, ICON_INPUT, 56, 56, p.surfaceAlt, p.text, false);
    lv_obj_align(input, LV_ALIGN_TOP_MID, 0, 156);

    theme::onChange([this, tile] {
        // Knopf-Hintergründe der Mitte neu einfärben (Wippen nutzen die Card-Styles)
        const theme::Palette& c = theme::colors();
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(tile); i++) {
            lv_obj_t* o = lv_obj_get_child(tile, i);
            if (lv_obj_get_user_data(o) == reinterpret_cast<void*>(1)) {
                lv_obj_set_style_bg_color(o, c.surfaceAlt, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(o, 0), c.text, 0);
            }
        }
    });
    lv_obj_set_user_data(mute, reinterpret_cast<void*>(1));
    lv_obj_set_user_data(input, reinterpret_cast<void*>(1));
}

lv_obj_t* IrRemoteApp::makeKey(lv_obj_t* parent, Key key, const char* symbol, lv_coord_t w, lv_coord_t h,
                               lv_color_t bg, lv_color_t fg, bool repeat) {
    lv_obj_t* b = ui::roundButton(parent, symbol, w, bg, fg, &font_title);
    lv_obj_set_size(b, w, h);
    s_ctx[key] = {this, key};
    lv_event_cb_t cb = [](lv_event_t* e) {
        auto* c = static_cast<KeyCtx*>(lv_event_get_user_data(e));
        c->app->send(c->key);
    };
    lv_obj_add_event_cb(b, cb, LV_EVENT_PRESSED, &s_ctx[key]);
    if (repeat) lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED_REPEAT, &s_ctx[key]);
    return b;
}

lv_obj_t* IrRemoteApp::makeRocker(lv_obj_t* tile, lv_coord_t x, const char* caption, Key up, Key down) {
    lv_obj_t* r = theme::cardObj(tile);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_radius(r, 31, 0);
    lv_obj_set_style_clip_corner(r, true, 0);
    lv_obj_set_size(r, 62, 164);
    lv_obj_set_pos(r, x, 64);

    auto half = [&](Key key, const char* sym, lv_align_t align) {
        lv_obj_t* b = lv_btn_create(r);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 62, 64);
        lv_obj_align(b, align, 0, 0);
        lv_obj_add_style(b, theme::cardPressed(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_t* l = theme::label(b, theme::textTitle(), sym);
        lv_obj_center(l);
        s_ctx[key] = {this, key};
        lv_event_cb_t cb = [](lv_event_t* e) {
            auto* c = static_cast<KeyCtx*>(lv_event_get_user_data(e));
            c->app->send(c->key);
        };
        lv_obj_add_event_cb(b, cb, LV_EVENT_PRESSED, &s_ctx[key]);
        lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED_REPEAT, &s_ctx[key]);
    };
    half(up, "+", LV_ALIGN_TOP_MID);
    half(down, "\xE2\x88\x92", LV_ALIGN_BOTTOM_MID);  // echtes Minuszeichen
    lv_obj_t* c = theme::label(r, theme::textCaption(), caption);
    lv_obj_center(c);
    return r;
}

void IrRemoteApp::updateBrandLabel() {
    lv_label_set_text_fmt(m_brandLabel, "%s  " LV_SYMBOL_DOWN, BRANDS[m_brand].name);
}

void IrRemoteApp::openBrandPicker() {
    ui::Sheet s = ui::openSheet("TV-Marke");
    for (uint8_t i = 0; i < BRAND_COUNT; i++) {
        lv_obj_t* row = theme::cardObj(s.content);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row, LV_PCT(100), 46);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_t* name = theme::label(row, theme::textBody(), BRANDS[i].name);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        if (i == m_brand) {
            lv_obj_t* check = theme::label(row, theme::textBody(), LV_SYMBOL_OK);
            lv_obj_add_style(check, theme::textAccent(), 0);
            lv_obj_align(check, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        lv_obj_set_user_data(row, reinterpret_cast<void*>(uintptr_t(i)));
        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                auto* self = static_cast<IrRemoteApp*>(lv_event_get_user_data(e));
                lv_obj_t* row = lv_event_get_current_target(e);
                self->m_brand = uint8_t(uintptr_t(lv_obj_get_user_data(row)));
                settings::putU8(KEY_BRAND, self->m_brand);
                self->updateBrandLabel();
                haptics::tap();
                ui::closeAllSheets();
            },
            LV_EVENT_CLICKED, this);
    }
}

void IrRemoteApp::send(Key key) {
    const Brand& b = BRANDS[m_brand];
    uint32_t code = b.codes[key];
    haptics::tap();
    switch (b.proto) {
        case Proto::Samsung:   s_ir.sendSAMSUNG(code, kSamsungBits); break;
        case Proto::NEC:       s_ir.sendNEC(code, kNECBits); break;
        case Proto::Sony:      s_ir.sendSony(code, kSony12Bits, kSonyMinRepeat); break;
        case Proto::Panasonic: s_ir.sendPanasonic(kPanasonicManufacturer, code); break;
        case Proto::RC6: {
            uint64_t data = s_ir.encodeRC6(0x00, uint8_t(code), kRC6Mode0Bits);
            if (m_rc6Toggle) data = s_ir.toggleRC6(data, kRC6Mode0Bits);
            m_rc6Toggle = !m_rc6Toggle;  // Toggle-Bit unterscheidet neuen Tastendruck
            s_ir.sendRC6(data, kRC6Mode0Bits);
            break;
        }
    }
}
