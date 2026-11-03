#include "watchfaces.h"

#include <math.h>

#include "core/data_store.h"
#include "hal/rtc_clock.h"
#include "ui/theme.h"
#include "ui/ui_core.h"

// -----------------------------------------------------------------------------
//  Gemeinsame Helfer
// -----------------------------------------------------------------------------
namespace {

const char* const WEEKDAY_SHORT[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
const char* const WEEKDAY_UPPER[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
const char* const WEEKDAY_LONG[]  = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                     "Donnerstag", "Freitag", "Samstag"};
const char* const WEEKDAY_CAPS[]  = {"SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH",
                                     "DONNERSTAG", "FREITAG", "SAMSTAG"};
const char* const MONTH_SHORT[]   = {"Jan.", "Feb.", "März", "Apr.", "Mai", "Juni",
                                     "Juli", "Aug.", "Sep.", "Okt.", "Nov.", "Dez."};
const char* const MONTH_CAPS[]    = {"JAN", "FEB", "MÄR", "APR", "MAI", "JUN",
                                     "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ"};

#define NNBSP "\xE2\x80\xAF"  // schmales geschütztes Leerzeichen: "82 %"
#define DASH  "\xE2\x80\x93"  // Halbgeviertstrich für "unbekannt"

constexpr lv_coord_t CX = 120;  // Display-Mitte
constexpr lv_coord_t CY = 120;

// Nur neu zeichnen, wenn sich der Text wirklich ändert
void setText(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

const char* batterySymbol(int pct, bool charging) {
    if (charging) return LV_SYMBOL_CHARGE;
    if (pct < 0)  return LV_SYMBOL_BATTERY_EMPTY;
    if (pct > 80) return LV_SYMBOL_BATTERY_FULL;
    if (pct > 55) return LV_SYMBOL_BATTERY_3;
    if (pct > 30) return LV_SYMBOL_BATTERY_2;
    if (pct > 10) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

// Akkufarbe wie iOS: grün beim Laden, rot ab 20 %, sonst neutral
lv_color_t batteryColor(int pct, bool charging) {
    const theme::Palette& p = theme::colors();
    if (charging) return p.green;
    if (pct >= 0 && pct <= 20) return p.red;
    return p.textSecondary;
}

void formatPercent(char* buf, size_t len, int pct) {
    if (pct < 0) snprintf(buf, len, DASH);
    else         snprintf(buf, len, "%d" NNBSP "%%", pct);
}

// Unsichtbarer Layout-Container (keine Klicks, kein Scrollen)
lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

}  // namespace

// -----------------------------------------------------------------------------
//  Registrierung
// -----------------------------------------------------------------------------
namespace watchfaces {

static DigitalFace s_digital;
static AnalogFace  s_analog;
static ModernFace  s_modern;
static InfoTile    s_info;

void registerColumn(uint8_t col) {
    ui::registerApp(col, 0, &s_digital);
    ui::registerApp(col, 1, &s_analog);
    ui::registerApp(col, 2, &s_modern);
    ui::registerApp(col, ui::WATCHFACE_COUNT, &s_info);
}

}  // namespace watchfaces

// =============================================================================
//  Y=0  DigitalFace
//
//          Dienstag, 3. Nov.        <- Headline, Orange
//            22:53                  <- 76 px Display-Schnitt
//         (BT)  [bat] 82 %          <- Caption, sekundär
// =============================================================================
void DigitalFace::create(lv_obj_t* tile) {
    m_date = theme::label(tile, theme::textHeadline());
    lv_obj_add_style(m_date, theme::textAccentAlt(), 0);

    m_time = theme::label(tile, theme::textTime(), "--:--");
    lv_obj_align(m_time, LV_ALIGN_CENTER, 0, -2);

    m_status = box(tile);
    lv_obj_set_size(m_status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(m_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_status, 6, 0);
    lv_obj_align(m_status, LV_ALIGN_CENTER, 0, 58);

    m_ble = theme::label(m_status, theme::textCaption(), LV_SYMBOL_BLUETOOTH);
    lv_obj_add_style(m_ble, theme::textAccent(), 0);
    lv_obj_set_style_pad_right(m_ble, 6, 0);
    m_batIcon  = theme::label(m_status, theme::textCaption(), LV_SYMBOL_BATTERY_FULL);
    m_batValue = theme::label(m_status, theme::textCaption(), DASH);
}

void DigitalFace::onTick() {
    char buf[48];
    struct tm t = rtc_clock::now();

    if (rtc_clock::isValid()) {
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        setText(m_time, buf);
        snprintf(buf, sizeof(buf), "%s, %d. %s", WEEKDAY_LONG[t.tm_wday], t.tm_mday, MONTH_SHORT[t.tm_mon]);
        setText(m_date, buf);
    } else {
        setText(m_time, "--:--");
        setText(m_date, "Zeit nicht gesetzt");
    }
    lv_obj_align(m_date, LV_ALIGN_CENTER, 0, -60);  // Breite ändert sich

    const WatchData& d = store::data();
    if (d.bleConnected) lv_obj_clear_flag(m_ble, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(m_ble, LV_OBJ_FLAG_HIDDEN);

    setText(m_batIcon, batterySymbol(d.watchBattery, d.watchCharging));
    lv_obj_set_style_text_color(m_batIcon, batteryColor(d.watchBattery, d.watchCharging), 0);
    formatPercent(buf, sizeof(buf), d.watchBattery);
    setText(m_batValue, buf);
}

// =============================================================================
//  Y=1  AnalogFace – 60er-Skala, Zeiger mit dünnem Steg (wie Apple "Einfach")
// =============================================================================
lv_obj_t* AnalogFace::makeLine(lv_obj_t* tile, lv_style_t* style, lv_coord_t width) {
    // Feste 240x240-Fläche -> Invalidierung deckt alte + neue Position ab
    lv_obj_t* l = lv_line_create(tile);
    lv_obj_add_style(l, style, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_size(l, 240, 240);
    lv_obj_set_pos(l, 0, 0);
    return l;
}

void AnalogFace::create(lv_obj_t* tile) {
    for (int i = 0; i < 60; i++) {
        float a    = i * 6.0f * DEG_TO_RAD;
        bool hour  = (i % 5) == 0;
        int rInner = hour ? 100 : 108;
        int rOuter = 115;
        m_tickPts[i][0] = {lv_coord_t(CX + rInner * sinf(a)), lv_coord_t(CY - rInner * cosf(a))};
        m_tickPts[i][1] = {lv_coord_t(CX + rOuter * sinf(a)), lv_coord_t(CY - rOuter * cosf(a))};

        lv_obj_t* tick = lv_line_create(tile);
        lv_obj_add_style(tick, hour ? theme::lineText() : theme::lineSecondary(), 0);
        lv_obj_set_style_line_width(tick, hour ? 4 : 2, 0);
        if (!hour) lv_obj_set_style_line_opa(tick, LV_OPA_70, 0);
        lv_line_set_points(tick, m_tickPts[i], 2);
    }

    // Datum bei 3 Uhr: Wochentag sekundär, Tag in Orange
    lv_obj_t* date = box(tile);
    lv_obj_set_size(date, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(date, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date, 4, 0);
    m_weekday = theme::label(date, theme::textHeadline(), "");
    m_day = theme::label(date, theme::textHeadline(), "");
    lv_obj_add_style(m_day, theme::textAccentAlt(), 0);
    lv_obj_align(date, LV_ALIGN_CENTER, 58, 0);

    m_hour.stem  = makeLine(tile, theme::lineText(), 3);
    m_hour.blade = makeLine(tile, theme::lineText(), 8);
    m_min.stem   = makeLine(tile, theme::lineText(), 3);
    m_min.blade  = makeLine(tile, theme::lineText(), 6);
    m_sec        = makeLine(tile, theme::lineAccent(), 2);

    lv_obj_t* hub = box(tile);
    lv_obj_add_style(hub, theme::fillAccentAlt(), 0);
    lv_obj_set_size(hub, 10, 10);
    lv_obj_center(hub);
    lv_obj_t* pin = box(tile);
    lv_obj_add_style(pin, theme::fillBg(), 0);
    lv_obj_set_size(pin, 4, 4);
    lv_obj_center(pin);
}

void AnalogFace::setHand(Hand& h, float angleDeg, int stemLen, int length) {
    float a = angleDeg * DEG_TO_RAD;
    float s = sinf(a), c = cosf(a);
    auto at = [&](int r) { return lv_point_t{lv_coord_t(CX + r * s), lv_coord_t(CY - r * c)}; };
    h.stemPts[0]  = at(0);
    h.stemPts[1]  = at(stemLen);
    h.bladePts[0] = at(stemLen);
    h.bladePts[1] = at(length);
    lv_line_set_points(h.stem, h.stemPts, 2);
    lv_line_set_points(h.blade, h.bladePts, 2);
}

void AnalogFace::onTick() {
    struct tm t = rtc_clock::now();
    float sec  = t.tm_sec;
    float min  = t.tm_min + sec / 60.0f;
    float hour = (t.tm_hour % 12) + min / 60.0f;

    setHand(m_hour, hour * 30.0f, 16, 60);
    setHand(m_min, min * 6.0f, 16, 94);

    float a = sec * 6.0f * DEG_TO_RAD;
    m_secPts[0] = {lv_coord_t(CX - 20 * sinf(a)), lv_coord_t(CY + 20 * cosf(a))};
    m_secPts[1] = {lv_coord_t(CX + 104 * sinf(a)), lv_coord_t(CY - 104 * cosf(a))};
    lv_line_set_points(m_sec, m_secPts, 2);

    char buf[8];
    setText(m_weekday, WEEKDAY_UPPER[t.tm_wday]);
    snprintf(buf, sizeof(buf), "%d", t.tm_mday);
    setText(m_day, buf);
}

// =============================================================================
//  Y=2  ModernFace – gestapelte Ziffern (Stunde weiß, Minute orange) + Ring
// =============================================================================
void ModernFace::create(lv_obj_t* tile) {
    m_ring = lv_arc_create(tile);
    lv_obj_set_size(m_ring, 232, 232);
    lv_obj_center(m_ring);
    lv_obj_remove_style(m_ring, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(m_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(m_ring, theme::arcTrack(), LV_PART_MAIN);
    lv_obj_add_style(m_ring, theme::arcIndicator(), LV_PART_INDICATOR);
    lv_arc_set_rotation(m_ring, 270);
    lv_arc_set_bg_angles(m_ring, 0, 360);
    lv_arc_set_range(m_ring, 0, 60);

    m_weekday = theme::label(tile, theme::textCaption());
    lv_obj_set_style_text_letter_space(m_weekday, 2, 0);

    m_hour = theme::label(tile, theme::textTimeXL(), "--");
    lv_obj_align(m_hour, LV_ALIGN_CENTER, 0, -36);

    m_minute = theme::label(tile, theme::textTimeXL(), "--");
    lv_obj_add_style(m_minute, theme::textAccentAlt(), 0);
    lv_obj_align(m_minute, LV_ALIGN_CENTER, 0, 38);

    m_date = theme::label(tile, theme::textCaption());
    lv_obj_set_style_text_letter_space(m_date, 2, 0);
}

void ModernFace::onTick() {
    struct tm t = rtc_clock::now();
    char buf[24];

    lv_arc_set_value(m_ring, t.tm_sec);

    snprintf(buf, sizeof(buf), "%02d", t.tm_hour);
    setText(m_hour, buf);
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    setText(m_minute, buf);

    setText(m_weekday, WEEKDAY_CAPS[t.tm_wday]);
    lv_obj_align(m_weekday, LV_ALIGN_CENTER, 0, -84);
    snprintf(buf, sizeof(buf), "%d. %s", t.tm_mday, MONTH_CAPS[t.tm_mon]);
    setText(m_date, buf);
    lv_obj_align(m_date, LV_ALIGN_CENTER, 0, 86);
}

// =============================================================================
//  Y=3  InfoTile – gruppierte Liste wie in den iOS-Einstellungen
//
//   ┌──────────────────────────────┐
//   │ [■] Akku            82 %  ›  │
//   │ [■] Standort                 │
//   │     48.1371, 11.5754         │
//   │ [■] Handy         Verbunden  │
//   │ [■] Dunkelmodus       (on)   │
//   └──────────────────────────────┘
// =============================================================================
namespace {

constexpr lv_coord_t LIST_W = 240 - 2 * theme::GAP;

// Zeile: [Icon] [Inhalt mit Trennlinie unten (iOS-Einzug)]
// Gibt den Inhalts-Container zurück (Flex-Row, Titel links, Trailing rechts).
lv_obj_t* listRow(lv_obj_t* list, const char* symbol, lv_color_t tint, bool last, lv_obj_t** rowOut = nullptr) {
    const theme::Palette& p = theme::colors();

    lv_obj_t* row = box(list);
    lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_column(row, 10, 0);

    theme::rowIcon(row, symbol, tint);

    lv_obj_t* content = box(row);
    lv_obj_set_height(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_right(content, 12, 0);
    lv_obj_set_style_pad_column(content, 6, 0);
    if (!last) {
        lv_obj_set_style_border_side(content, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(content, 1, 0);
        lv_obj_set_style_border_color(content, p.separator, 0);
        theme::onChange([content] { lv_obj_set_style_border_color(content, theme::colors().separator, 0); });
    }
    if (rowOut) *rowOut = row;
    return content;
}

lv_obj_t* rowTitle(lv_obj_t* content, const char* text) {
    lv_obj_t* l = theme::label(content, theme::textBody(), text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(l, 1);
    return l;
}

}  // namespace

void InfoTile::create(lv_obj_t* tile) {
    const theme::Palette& p = theme::colors();

    lv_obj_t* list = theme::cardObj(tile);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_set_size(list, LIST_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(list);

    // --- Akku (antippbar -> Übersicht) ---
    lv_obj_t* row;
    lv_obj_t* c = listRow(list, LV_SYMBOL_BATTERY_FULL, p.green, false, &row);
    m_batteryIcon = lv_obj_get_child(lv_obj_get_child(row, 0), 0);
    rowTitle(c, "Akku");
    m_batteryValue = theme::label(c, theme::textCaption(), DASH);
    theme::label(c, theme::textCaption(), ICON_CHEVRON);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(row, theme::cardPressed(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        row, [](lv_event_t* e) { static_cast<InfoTile*>(lv_event_get_user_data(e))->openBatteryModal(); },
        LV_EVENT_CLICKED, this);

    // --- Standort (Titel + Koordinaten darunter) ---
    c = listRow(list, ICON_LOCATION, theme::tint(theme::Tint::Blue), false);
    lv_obj_t* stack = box(c);
    lv_obj_set_flex_grow(stack, 1);
    lv_obj_set_height(stack, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    theme::label(stack, theme::textBody(), "Standort");
    m_location = theme::label(stack, theme::textCaption(), "Keine Daten");
    lv_label_set_long_mode(m_location, LV_LABEL_LONG_DOT);
    lv_obj_set_width(m_location, LV_PCT(100));

    // --- Handy-Verbindung ---
    c = listRow(list, LV_SYMBOL_BLUETOOTH, theme::tint(theme::Tint::Blue), false);
    rowTitle(c, "Handy");
    m_bleStatus = theme::label(c, theme::textCaption(), "");

    // --- Dunkelmodus ---
    c = listRow(list, ICON_MOON, theme::tint(theme::Tint::Indigo), true);
    rowTitle(c, "Dunkelmodus");
    m_darkSwitch = lv_switch_create(c);
    lv_obj_set_size(m_darkSwitch, 44, 26);
    lv_obj_set_ext_click_area(m_darkSwitch, 10);
    if (theme::isDark()) lv_obj_add_state(m_darkSwitch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(
        m_darkSwitch, [](lv_event_t* e) { theme::setDark(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); },
        LV_EVENT_VALUE_CHANGED, nullptr);

    store::subscribe(Topic::Battery,    [this] { refresh(); });
    store::subscribe(Topic::Location,   [this] { refresh(); });
    store::subscribe(Topic::Connection, [this] { refresh(); });
    refresh();
}

void InfoTile::refresh() {
    const WatchData& d = store::data();
    char buf[48];

    setText(m_batteryIcon, batterySymbol(d.watchBattery, d.watchCharging));
    formatPercent(buf, sizeof(buf), d.watchBattery);
    setText(m_batteryValue, buf);

    setText(m_location, d.location.length() ? d.location.c_str() : "Keine Daten");
    setText(m_bleStatus, d.bleConnected ? "Verbunden" : "Getrennt");

    if (m_modal) refreshModal();
}

// --- Akku-Übersicht ----------------------------------------------------------
void InfoTile::openBatteryModal() {
    if (m_modal) return;
    const theme::Palette& p = theme::colors();

    m_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(m_modal);
    lv_obj_add_style(m_modal, theme::screen(), 0);
    lv_obj_set_size(m_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(m_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(m_modal, 16, 0);
    lv_obj_set_style_pad_top(m_modal, 12, 0);
    lv_obj_set_style_pad_row(m_modal, 6, 0);
    lv_obj_clear_flag(m_modal, LV_OBJ_FLAG_SCROLLABLE);  // bleibt CLICKABLE -> blockiert Tileview
    lv_obj_move_foreground(m_modal);

    // Kopfzeile: Schließen-Knopf (watchOS: oben links) + Titel
    lv_obj_t* header = box(m_modal);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 10, 0);
    lv_obj_set_style_pad_bottom(header, 6, 0);

    lv_obj_t* close = theme::pillButton(header, ICON_CLOSE, false);
    lv_obj_set_size(close, 34, 34);
    lv_obj_set_style_pad_all(close, 0, 0);
    lv_obj_set_style_text_font(close, &font_caption, 0);
    lv_obj_set_ext_click_area(close, 12);
    lv_obj_add_event_cb(
        close, [](lv_event_t* e) { static_cast<InfoTile*>(lv_event_get_user_data(e))->closeBatteryModal(); },
        LV_EVENT_CLICKED, this);
    theme::label(header, theme::textTitle(), "Akkus");

    struct Device { const char* name; const char* icon; theme::Tint tint; };
    static const Device DEVICES[] = {
        {"Watch",   ICON_CLOCK,   theme::Tint::Orange},
        {"Handy",   ICON_PHONE,   theme::Tint::Blue},
        {"Ray-Ban", ICON_GLASSES, theme::Tint::Gray},
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* row = box(m_modal);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_style_pad_top(row, 4, 0);

        theme::rowIcon(row, DEVICES[i].icon, theme::tint(DEVICES[i].tint));
        lv_obj_t* name = theme::label(row, theme::textBody(), DEVICES[i].name);
        lv_obj_set_flex_grow(name, 1);
        m_rows[i].value = theme::label(row, theme::textHeadline(), DASH);

        lv_obj_t* bar = lv_bar_create(m_modal);
        lv_obj_set_size(bar, LV_PCT(100), 6);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, p.surfaceAlt, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        m_rows[i].bar = bar;
    }

    refreshModal();
    lv_obj_fade_in(m_modal, 180, 0);
}

void InfoTile::refreshModal() {
    const WatchData& d = store::data();
    const theme::Palette& p = theme::colors();
    const int values[3] = {d.watchBattery, d.phoneBattery, d.raybanBattery};

    for (int i = 0; i < 3; i++) {
        char buf[16];
        formatPercent(buf, sizeof(buf), values[i]);
        setText(m_rows[i].value, buf);
        int v = values[i] < 0 ? 0 : values[i];
        lv_bar_set_value(m_rows[i].bar, v, LV_ANIM_OFF);
        bool charging = i == 0 && d.watchCharging;
        lv_obj_set_style_bg_color(m_rows[i].bar, (v <= 20 && !charging) ? p.red : p.green, LV_PART_INDICATOR);
    }
}

void InfoTile::closeBatteryModal() {
    if (!m_modal) return;
    lv_obj_del_async(m_modal);  // async: wir können aus dem Click-Event eines Kindes kommen
    m_modal = nullptr;
}
