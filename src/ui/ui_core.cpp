#include "ui_core.h"

#include <time.h>

#include <vector>

#include "core/settings.h"
#include "ui/map_screen.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

struct Entry {
    uint8_t col;
    uint8_t row;
    WatchApp* app;
    lv_obj_t* tile;
};

static std::vector<Entry> s_entries;
static lv_obj_t* s_tv           = nullptr;
static Entry* s_current         = nullptr;
static uint8_t s_lastFace       = 0;
static bool s_redirectPending   = false;
static time_t s_lastTick        = 0;

// Seitenpunkte (watchOS-Stil): kurz einblenden nach jedem Kachelwechsel
static lv_obj_t* s_dotsH        = nullptr;  // Spalten, unten
static lv_obj_t* s_dotsV        = nullptr;  // Reihen der aktuellen Spalte, rechts
static constexpr uint32_t DOTS_VISIBLE_MS = 900;
static constexpr uint32_t DOTS_FADE_MS    = 250;

static Entry* find(int col, int row) {
    for (auto& e : s_entries)
        if (e.col == col && e.row == row) return &e;
    return nullptr;
}

static Entry* findByTile(lv_obj_t* tile) {
    for (auto& e : s_entries)
        if (e.tile == tile) return &e;
    return nullptr;
}

static bool isWatchface(const Entry* e) {
    return e && e->col == COL_WATCHFACES && e->row < WATCHFACE_COUNT;
}

static uint8_t columnCount() {
    uint8_t n = 0;
    for (auto& e : s_entries) n = e.col + 1 > n ? e.col + 1 : n;
    return n;
}

static uint8_t rowCount(uint8_t col) {
    uint8_t n = 0;
    for (auto& e : s_entries)
        if (e.col == col) n = e.row + 1 > n ? e.row + 1 : n;
    return n;
}

static lv_obj_t* makeDots(lv_flex_flow_t flow) {
    lv_obj_t* c = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, flow);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(c, 5, 0);
    lv_obj_set_style_pad_gap(c, 5, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_50, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(c, LV_OPA_TRANSP, 0);
    return c;
}

static void fillDots(lv_obj_t* c, uint8_t count, uint8_t active) {
    const theme::Palette& p = theme::colors();
    lv_obj_clean(c);
    lv_obj_set_style_bg_color(c, p.bg, 0);
    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t* d = lv_obj_create(c);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, i == active ? p.text : p.textSecondary, 0);
        if (i != active) lv_obj_set_style_opa(d, LV_OPA_60, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void flashDots(lv_obj_t* c, bool visible) {
    lv_anim_del(c, nullptr);
    if (!visible) {
        lv_obj_set_style_opa(c, LV_OPA_TRANSP, 0);
        return;
    }
    lv_obj_set_style_opa(c, LV_OPA_COVER, 0);
    lv_obj_fade_out(c, DOTS_FADE_MS, DOTS_VISIBLE_MS);
}

static void showIndicator(const Entry* e) {
    uint8_t cols = columnCount();
    uint8_t rows = rowCount(e->col);

    fillDots(s_dotsH, cols, e->col);
    lv_obj_align(s_dotsH, LV_ALIGN_BOTTOM_MID, 0, -4);
    flashDots(s_dotsH, cols > 1);

    fillDots(s_dotsV, rows, e->row);
    lv_obj_align(s_dotsV, LV_ALIGN_RIGHT_MID, -3, 0);
    flashDots(s_dotsV, rows > 1);
}

// Kachel wird aktiv: Lifecycle-Callbacks + Watchface-Position merken
static void activate(Entry* next, bool indicate) {
    if (!next || next == s_current) return;
    if (s_current) s_current->app->onHide();
    setSwipeLocked(false);  // Sperre gilt nur für die Kachel, die sie gesetzt hat
    s_current = next;

    if (isWatchface(next) && next->row != s_lastFace) {
        s_lastFace = next->row;
        settings::setLastWatchface(s_lastFace);
    }
    next->app->onShow();
    if (indicate) showIndicator(next);
}

static void jumpTo(Entry* e, bool animate, bool indicate) {
    if (!e || !s_tv) return;
    lv_obj_set_tile(s_tv, e->tile, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    activate(e, indicate);  // set_tile sendet ohne Animation nicht zuverlässig VALUE_CHANGED
}

static void redirectToWatchface(void*) {
    s_redirectPending = false;
    jumpTo(find(COL_WATCHFACES, s_lastFace), false, true);
}

static void onTileChanged(lv_event_t*) {
    Entry* next = findByTile(lv_tileview_get_tile_act(s_tv));
    if (!next || next == s_current) return;

    // Rückkehr aus einer anderen Spalte in die Watchface-Spalte:
    // nicht auf der "Durchgangs"-Reihe bleiben, sondern zum gemerkten Watchface springen.
    bool enteringFaces = next->col == COL_WATCHFACES && s_current && s_current->col != COL_WATCHFACES;
    if (enteringFaces && next->row != s_lastFace) {
        if (!s_redirectPending) {
            s_redirectPending = true;
            lv_async_call(redirectToWatchface, nullptr);  // nicht im Scroll-Event selbst scrollen
        }
        return;
    }
    activate(next, true);
}

void registerApp(uint8_t col, uint8_t row, WatchApp* app) {
    if (s_tv) {
        log_e("registerApp(%u,%u) nach build() – ignoriert", col, row);
        return;
    }
    if (Entry* e = find(col, row)) {
        e->app = app;  // überschreiben erlaubt (z.B. Platzhalter -> echte App)
        return;
    }
    s_entries.push_back({col, row, app, nullptr});
}

void build() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_add_style(scr, theme::screen(), 0);

    s_tv = lv_tileview_create(scr);
    lv_obj_add_style(s_tv, theme::screen(), 0);
    lv_obj_set_size(s_tv, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);

    for (auto& e : s_entries) {
        lv_dir_t dir = LV_DIR_NONE;
        if (e.col > 0 && find(e.col - 1, e.row)) dir |= LV_DIR_LEFT;
        if (find(e.col + 1, e.row))             dir |= LV_DIR_RIGHT;
        if (e.row > 0 && find(e.col, e.row - 1)) dir |= LV_DIR_TOP;
        if (find(e.col, e.row + 1))             dir |= LV_DIR_BOTTOM;

        e.tile = lv_tileview_add_tile(s_tv, e.col, e.row, dir);
        lv_obj_set_scrollbar_mode(e.tile, LV_SCROLLBAR_MODE_OFF);
        e.app->create(e.tile);
    }

    lv_obj_add_event_cb(s_tv, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    s_dotsH = makeDots(LV_FLEX_FLOW_ROW);
    s_dotsV = makeDots(LV_FLEX_FLOW_COLUMN);

    s_lastFace = settings::lastWatchface();
    if (s_lastFace >= WATCHFACE_COUNT || !find(COL_WATCHFACES, s_lastFace)) s_lastFace = 0;
    showWatchface(false);
}

void loop(bool displayOn) {
    if (!displayOn || !s_current) return;
    // Tick an der Sekundengrenze der Systemuhr ausrichten
    time_t now = time(nullptr);
    if (now == s_lastTick) return;
    s_lastTick = now;
    s_current->app->onTick();
}

void showTile(uint8_t col, uint8_t row, bool animate) {
    jumpTo(find(col, row), animate, false);
}

void showWatchface(bool animate) { showTile(COL_WATCHFACES, s_lastFace, animate); }

void onWake() {
    // Wie bei watchOS: nach dem Aufwachen keine offenen Detailansichten mehr
    ui::closeAllSheets();
    mapscreen::close();
    if (isWatchface(s_current) && s_current->row == s_lastFace) {
        s_current->app->onShow();  // schon da -> nur Inhalte auffrischen
    } else {
        showWatchface(false);
    }
}

lv_obj_t* tileview() { return s_tv; }

bool isSwipeLocked() { return s_tv && !lv_obj_has_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE); }

void setSwipeLocked(bool locked) {
    if (!s_tv) return;
    if (locked) lv_obj_clear_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
    else        lv_obj_add_flag(s_tv, LV_OBJ_FLAG_SCROLLABLE);
}

bool dispatchButton() {
    return s_current && s_current->app->onButton();
}

}  // namespace ui
