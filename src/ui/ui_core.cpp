#include "ui_core.h"

#include <time.h>

#include <vector>

#include "core/settings.h"
#include "ui/theme.h"

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

// Kachel wird aktiv: Lifecycle-Callbacks + Watchface-Position merken
static void activate(Entry* next) {
    if (!next || next == s_current) return;
    if (s_current) s_current->app->onHide();
    s_current = next;

    if (isWatchface(next) && next->row != s_lastFace) {
        s_lastFace = next->row;
        settings::setLastWatchface(s_lastFace);
    }
    next->app->onShow();
}

static void redirectToWatchface(void*) {
    s_redirectPending = false;
    showWatchface(false);
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
    activate(next);
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
    Entry* e = find(col, row);
    if (!e || !s_tv) return;
    lv_obj_set_tile(s_tv, e->tile, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    activate(e);  // set_tile sendet ohne Animation nicht zuverlässig VALUE_CHANGED
}

void showWatchface(bool animate) { showTile(COL_WATCHFACES, s_lastFace, animate); }

void onWake() {
    if (isWatchface(s_current) && s_current->row == s_lastFace) {
        s_current->app->onShow();  // schon da -> nur Inhalte auffrischen
    } else {
        showWatchface(false);
    }
}

lv_obj_t* tileview() { return s_tv; }

}  // namespace ui
