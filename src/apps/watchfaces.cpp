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

// Hinweis: Die eingebauten Montserrat-Fonts haben keine Umlaute -> "Mrz" statt "März".
const char* const WEEKDAY_SHORT[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
const char* const WEEKDAY_LONG[]  = {"SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH",
                                     "DONNERSTAG", "FREITAG", "SAMSTAG"};
const char* const MONTH_SHORT[]   = {"Jan", "Feb", "Mrz", "Apr", "Mai", "Jun",
                                     "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};

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

void formatPercent(char* buf, size_t len, int pct) {
    if (pct < 0) snprintf(buf, len, "--");
    else         snprintf(buf, len, "%d%%", pct);
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
//  Y=0  DigitalFace – große Uhrzeit, Datum in Orange, Statuszeile
// =============================================================================
void DigitalFace::create(lv_obj_t* tile) {
    m_date = theme::label(tile, theme::textBody());
    lv_obj_add_style(m_date, theme::textAccentAlt(), 0);
    lv_obj_align(m_date, LV_ALIGN_CENTER, 0, -52);

    m_time = theme::label(tile, theme::textLarge(), "--:--");
    lv_obj_align(m_time, LV_ALIGN_CENTER, 0, 0);

    m_status = theme::label(tile, theme::textCaption());
    lv_obj_align(m_status, LV_ALIGN_CENTER, 0, 52);
}

void DigitalFace::onTick() {
    char buf[48];
    struct tm t = rtc_clock::now();

    if (rtc_clock::isValid()) {
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        setText(m_time, buf);
        snprintf(buf, sizeof(buf), "%s, %d. %s", WEEKDAY_SHORT[t.tm_wday], t.tm_mday, MONTH_SHORT[t.tm_mon]);
        setText(m_date, buf);
    } else {
        setText(m_time, "--:--");
        setText(m_date, "Zeit nicht gesetzt");
    }

    const WatchData& d = store::data();
    char pct[8];
    formatPercent(pct, sizeof(pct), d.watchBattery);
    snprintf(buf, sizeof(buf), "%s  %s %s",
             d.bleConnected ? LV_SYMBOL_BLUETOOTH : "",
             batterySymbol(d.watchBattery, d.watchCharging), pct);
    setText(m_status, buf);
    lv_obj_align(m_status, LV_ALIGN_CENTER, 0, 52);  // Breite ändert sich
}

// =============================================================================
//  Y=1  AnalogFace – 12 Indizes, Stunden/Minuten/Sekunden-Zeiger als lv_line
// =============================================================================
void AnalogFace::create(lv_obj_t* tile) {
    for (int i = 0; i < 12; i++) {
        float a     = i * 30.0f * DEG_TO_RAD;
        bool major  = (i % 3) == 0;
        int rInner  = major ? 96 : 104;
        int rOuter  = 112;
        m_tickPts[i][0] = {lv_coord_t(CX + rInner * sinf(a)), lv_coord_t(CY - rInner * cosf(a))};
        m_tickPts[i][1] = {lv_coord_t(CX + rOuter * sinf(a)), lv_coord_t(CY - rOuter * cosf(a))};

        lv_obj_t* tick = lv_line_create(tile);
        lv_obj_add_style(tick, major ? theme::lineText() : theme::lineSecondary(), 0);
        lv_line_set_points(tick, m_tickPts[i], 2);
    }

    m_date = theme::label(tile, theme::textCaption());
    lv_obj_align(m_date, LV_ALIGN_CENTER, 0, 48);

    // Zeiger: feste 240x240-Fläche -> Invalidierung deckt alte + neue Position ab
    auto makeHand = [&](lv_style_t* style, lv_coord_t width) {
        lv_obj_t* l = lv_line_create(tile);
        lv_obj_add_style(l, style, 0);
        lv_obj_set_style_line_width(l, width, 0);
        lv_obj_set_size(l, 240, 240);
        lv_obj_set_pos(l, 0, 0);
        return l;
    };
    m_hour = makeHand(theme::lineText(), 7);
    m_min  = makeHand(theme::lineText(), 5);
    m_sec  = makeHand(theme::lineAccent(), 2);

    lv_obj_t* hub = lv_obj_create(tile);
    lv_obj_remove_style_all(hub);
    lv_obj_add_style(hub, theme::fillAccentAlt(), 0);
    lv_obj_set_size(hub, 10, 10);
    lv_obj_center(hub);
}

void AnalogFace::setHand(lv_obj_t* line, lv_point_t* pts, float angleDeg, int length, int tail) {
    float a = angleDeg * DEG_TO_RAD;
    float s = sinf(a), c = cosf(a);
    pts[0] = {lv_coord_t(CX - tail * s), lv_coord_t(CY + tail * c)};
    pts[1] = {lv_coord_t(CX + length * s), lv_coord_t(CY - length * c)};
    lv_line_set_points(line, pts, 2);
}

void AnalogFace::onTick() {
    struct tm t = rtc_clock::now();
    float sec  = t.tm_sec;
    float min  = t.tm_min + sec / 60.0f;
    float hour = (t.tm_hour % 12) + min / 60.0f;

    setHand(m_hour, m_hourPts, hour * 30.0f, 55, 0);
    setHand(m_min,  m_minPts,  min * 6.0f,   86, 0);
    setHand(m_sec,  m_secPts,  sec * 6.0f,   96, 16);

    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", WEEKDAY_SHORT[t.tm_wday], t.tm_mday);
    setText(m_date, buf);
}

// =============================================================================
//  Y=2  ModernFace – gestapelte Stunden/Minuten, Sekunden-Ring
// =============================================================================
void ModernFace::create(lv_obj_t* tile) {
    m_ring = lv_arc_create(tile);
    lv_obj_set_size(m_ring, 228, 228);
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
    lv_obj_align(m_weekday, LV_ALIGN_CENTER, 0, -70);

    m_hour = theme::label(tile, theme::textLarge(), "--");
    lv_obj_align(m_hour, LV_ALIGN_CENTER, 0, -24);

    m_minute = theme::label(tile, theme::textLarge(), "--");
    lv_obj_add_style(m_minute, theme::textAccentAlt(), 0);
    lv_obj_align(m_minute, LV_ALIGN_CENTER, 0, 26);

    m_date = theme::label(tile, theme::textCaption());
    lv_obj_align(m_date, LV_ALIGN_CENTER, 0, 70);
}

void ModernFace::onTick() {
    struct tm t = rtc_clock::now();
    char buf[24];

    lv_arc_set_value(m_ring, t.tm_sec);

    snprintf(buf, sizeof(buf), "%02d", t.tm_hour);
    setText(m_hour, buf);
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    setText(m_minute, buf);

    setText(m_weekday, WEEKDAY_LONG[t.tm_wday]);
    lv_obj_align(m_weekday, LV_ALIGN_CENTER, 0, -70);
    snprintf(buf, sizeof(buf), "%d. %s", t.tm_mday, MONTH_SHORT[t.tm_mon]);
    setText(m_date, buf);
    lv_obj_align(m_date, LV_ALIGN_CENTER, 0, 70);
}

// =============================================================================
//  Y=3  InfoTile – Akku (Tap -> Overlay), Standort, Status + Dark Mode
// =============================================================================
static lv_obj_t* makeRow(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

void InfoTile::create(lv_obj_t* tile) {
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tile, theme::PAD, 0);
    lv_obj_set_style_pad_row(tile, 8, 0);

    auto makeCard = [&](lv_obj_t* parent) {
        lv_obj_t* c = theme::cardObj(parent);
        lv_obj_set_width(c, LV_PCT(100));
        lv_obj_set_flex_grow(c, 1);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        return c;
    };

    // --- Akku ---
    lv_obj_t* battery = makeCard(tile);
    lv_obj_add_flag(battery, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* row = makeRow(battery);
    m_batteryIcon  = theme::label(row, theme::textTitle(), LV_SYMBOL_BATTERY_FULL);
    lv_obj_add_style(m_batteryIcon, theme::textAccentAlt(), 0);
    m_batteryValue = theme::label(row, theme::textTitle(), "--");
    lv_obj_set_flex_grow(m_batteryValue, 1);
    theme::label(row, theme::textCaption(), LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(
        battery, [](lv_event_t* e) { static_cast<InfoTile*>(lv_event_get_user_data(e))->openBatteryModal(); },
        LV_EVENT_CLICKED, this);

    // --- Standort ---
    lv_obj_t* loc = makeCard(tile);
    row = makeRow(loc);
    lv_obj_t* gps = theme::label(row, theme::textBody(), LV_SYMBOL_GPS);
    lv_obj_add_style(gps, theme::textAccent(), 0);
    theme::label(row, theme::textCaption(), "Koordinaten");
    m_location = theme::label(loc, theme::textBody(), "Keine Daten");
    lv_label_set_long_mode(m_location, LV_LABEL_LONG_DOT);
    lv_obj_set_width(m_location, LV_PCT(100));

    // --- Weitere Infos: BLE + Dark Mode ---
    lv_obj_t* more = makeCard(tile);
    row = makeRow(more);
    lv_obj_t* dmLabel = theme::label(row, theme::textBody(), "Dark Mode");
    lv_obj_set_flex_grow(dmLabel, 1);
    m_darkSwitch = lv_switch_create(row);
    lv_obj_set_size(m_darkSwitch, 46, 26);
    if (theme::isDark()) lv_obj_add_state(m_darkSwitch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(
        m_darkSwitch, [](lv_event_t* e) { theme::setDark(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); },
        LV_EVENT_VALUE_CHANGED, nullptr);
    m_bleStatus = theme::label(more, theme::textCaption());

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

    snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH " %s", d.bleConnected ? "Handy verbunden" : "Nicht verbunden");
    setText(m_bleStatus, buf);

    if (m_modal) refreshModal();
}

void InfoTile::openBatteryModal() {
    if (m_modal) return;
    const theme::Palette& p = theme::colors();

    m_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(m_modal);
    lv_obj_add_style(m_modal, theme::screen(), 0);
    lv_obj_set_size(m_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(m_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(m_modal, 16, 0);
    lv_obj_set_style_pad_row(m_modal, 6, 0);
    lv_obj_clear_flag(m_modal, LV_OBJ_FLAG_SCROLLABLE);  // bleibt CLICKABLE -> blockiert Tileview

    theme::label(m_modal, theme::textTitle(), "Akkus");

    static const char* const NAMES[] = {"Watch", "Handy", "Ray-Ban"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* row = makeRow(m_modal);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_t* name = theme::label(row, theme::textBody(), NAMES[i]);
        lv_obj_set_flex_grow(name, 1);
        m_rows[i].value = theme::label(row, theme::textCaption(), "--");

        lv_obj_t* bar = lv_bar_create(m_modal);
        lv_obj_set_size(bar, LV_PCT(100), 8);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar, p.surfaceAlt, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        m_rows[i].bar = bar;
    }

    lv_obj_t* spacer = lv_obj_create(m_modal);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* done = lv_btn_create(m_modal);
    lv_obj_remove_style_all(done);
    lv_obj_add_style(done, theme::buttonAccent(), 0);
    lv_obj_add_style(done, theme::pressed(), LV_STATE_PRESSED);
    lv_obj_set_width(done, LV_PCT(100));
    lv_obj_t* doneLabel = lv_label_create(done);
    lv_label_set_text(doneLabel, "Fertig");
    lv_obj_center(doneLabel);
    lv_obj_add_event_cb(
        done, [](lv_event_t* e) { static_cast<InfoTile*>(lv_event_get_user_data(e))->closeBatteryModal(); },
        LV_EVENT_CLICKED, this);

    refreshModal();
    lv_obj_fade_in(m_modal, 150, 0);
}

void InfoTile::refreshModal() {
    const WatchData& d = store::data();
    const theme::Palette& p = theme::colors();
    const int values[3] = {d.watchBattery, d.phoneBattery, d.raybanBattery};

    for (int i = 0; i < 3; i++) {
        char buf[8];
        formatPercent(buf, sizeof(buf), values[i]);
        setText(m_rows[i].value, buf);
        int v = values[i] < 0 ? 0 : values[i];
        lv_bar_set_value(m_rows[i].bar, v, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(m_rows[i].bar, v <= 20 ? p.red : p.green, LV_PART_INDICATOR);
    }
}

void InfoTile::closeBatteryModal() {
    if (!m_modal) return;
    lv_obj_del_async(m_modal);  // async: wir können aus dem Click-Event eines Kindes kommen
    m_modal = nullptr;
}
