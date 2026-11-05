#include "media_app.h"

#include "core/data_store.h"
#include "hal/haptics.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

void formatTime(char* buf, size_t len, uint32_t s) { snprintf(buf, len, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60)); }

struct Cmd {
    MediaApp* app;
    const char* cmd;
};

}  // namespace

void MediaApp::create(lv_obj_t* tile) {
    const theme::Palette& p = theme::colors();

    m_empty = ui::emptyState(tile, ICON_SPOTIFY, theme::tint(theme::Tint::Spotify), "Nichts wird abgespielt",
                             "Starte Musik auf dem Handy");
    lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(m_empty, 0), 0), lv_color_black(), 0);

    m_player = ui::box(tile);
    lv_obj_set_size(m_player, LV_PCT(100), LV_PCT(100));

    // Quellen-Leiste: aktuelle App (grün) + andere startbare Apps als "▶ Spotify"-Pillen
    m_srcRow = ui::box(m_player);
    lv_obj_set_size(m_srcRow, 230, 26);
    lv_obj_set_flex_flow(m_srcRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_srcRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_srcRow, 6, 0);
    lv_obj_add_flag(m_srcRow, LV_OBJ_FLAG_CLICKABLE);   // box() nimmt Klicks weg; Kinder brauchen sie
    lv_obj_align(m_srcRow, LV_ALIGN_TOP_MID, 0, 8);

    m_emptySrc = ui::box(tile);
    lv_obj_set_size(m_emptySrc, 230, 34);
    lv_obj_set_flex_flow(m_emptySrc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_emptySrc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_emptySrc, 8, 0);
    lv_obj_add_flag(m_emptySrc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(m_emptySrc, LV_ALIGN_BOTTOM_MID, 0, -14);

    m_title = theme::label(m_player, theme::textTitle(), "");
    lv_label_set_long_mode(m_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(m_title, 204);
    lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m_title, LV_ALIGN_TOP_MID, 0, 36);

    m_artist = theme::label(m_player, theme::textCaption(), "");
    lv_label_set_long_mode(m_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_width(m_artist, 204);
    lv_obj_set_style_text_align(m_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m_artist, LV_ALIGN_TOP_MID, 0, 68);

    // Fortschritt
    m_bar = lv_bar_create(m_player);
    lv_obj_set_size(m_bar, 196, 4);
    lv_obj_align(m_bar, LV_ALIGN_TOP_MID, 0, 98);
    lv_obj_set_style_radius(m_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(m_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_bar, p.surfaceAlt, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_bar, p.text, LV_PART_INDICATOR);
    lv_bar_set_range(m_bar, 0, 1000);
    theme::onChange([this] {
        const theme::Palette& c = theme::colors();
        lv_obj_set_style_bg_color(m_bar, c.surfaceAlt, LV_PART_MAIN);
        lv_obj_set_style_bg_color(m_bar, c.text, LV_PART_INDICATOR);
    });
    m_posLabel = theme::label(m_player, theme::textCaption(), "0:00");
    lv_obj_align(m_posLabel, LV_ALIGN_TOP_LEFT, 22, 106);
    m_durLabel = theme::label(m_player, theme::textCaption(), "0:00");
    lv_obj_align(m_durLabel, LV_ALIGN_TOP_RIGHT, -22, 106);

    // Steuerung: zurück | play/pause | vor
    static Cmd cmds[5];
    auto button = [&](int idx, const char* cmd, const char* sym, lv_coord_t size, bool prominent, lv_coord_t x,
                      lv_coord_t y) {
        lv_obj_t* b = ui::roundButton(m_player, sym, size, prominent ? p.text : p.bg, prominent ? p.bg : p.text,
                                      prominent ? &font_title : &font_headline);
        lv_obj_align(b, LV_ALIGN_TOP_MID, x, y);
        cmds[idx] = {this, cmd};
        lv_obj_add_event_cb(
            b, [](lv_event_t* e) {
                auto* c = static_cast<Cmd*>(lv_event_get_user_data(e));
                c->app->command(c->cmd);
            },
            LV_EVENT_CLICKED, &cmds[idx]);
        lv_obj_set_ext_click_area(b, 6);
        return b;
    };
    button(0, "prev", LV_SYMBOL_PREV, 48, false, -70, 138);
    lv_obj_t* play = button(1, "toggle", LV_SYMBOL_PLAY, 64, true, 0, 130);
    m_playIcon = lv_obj_get_child(play, 0);
    button(2, "next", LV_SYMBOL_NEXT, 48, false, 70, 138);

    // Lautstärke (kleine Pillen unten)
    lv_obj_t* vd = button(3, "vol_down", LV_SYMBOL_VOLUME_MID, 40, false, -36, 196);
    lv_obj_t* vu = button(4, "vol_up", LV_SYMBOL_VOLUME_MAX, 40, false, 36, 196);
    for (lv_obj_t* v : {vd, vu}) {
        lv_obj_set_style_bg_color(v, p.surface, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(v, 0), &font_caption, 0);
    }
    theme::onChange([this, play, vd, vu] {
        const theme::Palette& c = theme::colors();
        lv_obj_set_style_bg_color(play, c.text, 0);
        lv_obj_set_style_text_color(m_playIcon, c.bg, 0);
        for (lv_obj_t* v : {vd, vu}) {
            lv_obj_set_style_bg_color(v, c.surface, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(v, 0), c.text, 0);
        }
        // prev/next: Hintergrund = bg
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(m_player); i++) {
            lv_obj_t* o = lv_obj_get_child(m_player, i);
            if (lv_obj_check_type(o, &lv_btn_class) && o != play && o != vd && o != vu) {
                lv_obj_set_style_bg_color(o, c.bg, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(o, 0), c.text, 0);
            }
        }
    });

    store::subscribe(Topic::Media, [this] { refresh(); });
    store::subscribe(Topic::Connection, [this] { refresh(); });
    refresh();
}

void MediaApp::command(const char* cmd) {
    haptics::tap();
    if (!store::sendToPhone("MEDIA", cmd)) return;
    // Sofortiges Feedback: Play/Pause lokal umschalten, Handy bestätigt per MEDIA|...
    if (strcmp(cmd, "toggle") == 0) {
        MediaState& m = store::data().media;
        m.posSec  = m.positionNow();
        m.rxMs    = millis();
        m.playing = !m.playing;
        refresh();
    }
}

namespace {

// Pille für eine Quelle. current = gerade angezeigt (nur Text, grün), sonst antippbar "▶ Name".
lv_obj_t* sourcePill(lv_obj_t* row, const String& name, bool current, bool big) {
    const theme::Palette& p = theme::colors();
    lv_obj_t* pill = lv_obj_create(row);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, big ? 34 : 24);
    lv_obj_set_style_pad_hor(pill, current ? 4 : 10, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(pill);
    lv_obj_set_style_text_font(l, &font_caption, 0);
    lv_obj_center(l);
    if (current) {
        lv_label_set_text(l, name.c_str());
        lv_obj_set_style_text_color(l, theme::tint(theme::Tint::Spotify), 0);
        lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    } else {
        String t = String(LV_SYMBOL_PLAY " ") + name;
        lv_label_set_text(l, t.c_str());
        lv_obj_set_style_text_color(l, p.text, 0);
        lv_obj_set_style_bg_color(pill, p.surfaceAlt, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, p.separator, LV_STATE_PRESSED);
        lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(pill, 6);
        // Name am Objekt merken (kopiert, wird beim Löschen freigegeben)
        char* copy = strdup(name.c_str());
        lv_obj_add_event_cb(pill, [](lv_event_t* e) {
            const char* n = static_cast<const char*>(lv_event_get_user_data(e));
            haptics::tap();
            store::sendToPhone("MEDIA", String("src|") + n);
        }, LV_EVENT_CLICKED, copy);
        lv_obj_add_event_cb(pill, [](lv_event_t* e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, copy);
    }
    return pill;
}

}  // namespace

void MediaApp::buildSources() {
    const WatchData& d = store::data();
    String cur = d.media.active ? (d.mediaCurrent.length() ? d.mediaCurrent : d.media.app) : String("");
    String key = cur + "#" + String(d.media.active);
    for (auto& s : d.mediaSources) key += "|" + s;
    if (key == m_srcKey) return;
    m_srcKey = key;

    lv_obj_clean(m_srcRow);
    lv_obj_clean(m_emptySrc);
    if (d.media.active) sourcePill(m_srcRow, cur, true, false);
    int others = 0;
    for (auto& s : d.mediaSources) {
        if (d.media.active && s == cur) continue;
        if (others >= 2) break;
        // Im Player nur eine Alternative (Platz), im leeren Zustand bis zu zwei
        if (d.media.active && others >= 1) break;
        sourcePill(d.media.active ? m_srcRow : m_emptySrc, s, false, !d.media.active);
        others++;
    }
}

void MediaApp::refresh() {
    const WatchData& d = store::data();
    const MediaState& m = d.media;
    buildSources();

    if (!m.active) {
        lv_obj_add_flag(m_player, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* sub = lv_obj_get_child(m_empty, 2);
        ui::setText(sub, !d.bleConnected ? "Handy nicht verbunden"
                         : d.mediaSources.empty() ? "Starte Musik auf dem Handy" : "Weiter hören mit");
        if (d.bleConnected) lv_obj_clear_flag(m_emptySrc, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(m_emptySrc, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(m_player, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_emptySrc, LV_OBJ_FLAG_HIDDEN);
    ui::setText(m_title, m.title.length() ? m.title.c_str() : "Unbekannter Titel");
    ui::setText(m_artist, m.artist.c_str());
    ui::setText(m_playIcon, m.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    updateProgress();
}

void MediaApp::updateProgress() {
    const MediaState& m = store::data().media;
    if (!m.active) return;
    char buf[12];
    uint32_t pos = m.positionNow();
    formatTime(buf, sizeof(buf), pos);
    ui::setText(m_posLabel, buf);
    formatTime(buf, sizeof(buf), m.durSec);
    ui::setText(m_durLabel, m.durSec ? buf : UI_DASH);
    lv_bar_set_value(m_bar, m.durSec ? int(pos * 1000 / m.durSec) : 0, LV_ANIM_OFF);
}
