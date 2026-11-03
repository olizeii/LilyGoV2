#include "messages_app.h"

#include "core/data_store.h"
#include "hal/haptics.h"
#include "hal/rtc_clock.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

struct AppStyle {
    const char* match;  // Teilstring im App-Namen (klein geschrieben)
    theme::Tint tint;
    bool darkGlyph;
};
const AppStyle APP_STYLES[] = {
    {"whatsapp", theme::Tint::Green, false},  {"signal", theme::Tint::Blue, false},
    {"telegram", theme::Tint::Cyan, false},   {"nachrichten", theme::Tint::Green, false},
    {"messages", theme::Tint::Green, false},  {"instagram", theme::Tint::Pink, false},
    {"gmail", theme::Tint::Red, false},       {"mail", theme::Tint::Blue, false},
    {"discord", theme::Tint::Indigo, false},  {"snapchat", theme::Tint::Yellow, true},
    {"spotify", theme::Tint::Spotify, true},  {"untis", theme::Tint::Orange, false},
    {"kalender", theme::Tint::Red, false},    {"calendar", theme::Tint::Red, false},
    {"telefon", theme::Tint::Green, false},   {"phone", theme::Tint::Green, false},
};

const PhoneMessage* findMessage(const String& key) {
    for (auto& m : store::data().messages)
        if (m.key == key) return &m;
    return nullptr;
}

}  // namespace

lv_obj_t* MessagesApp::appBadge(lv_obj_t* parent, const String& app, lv_coord_t size) {
    String lower = app;
    lower.toLowerCase();
    theme::Tint tint = theme::Tint::Gray;
    bool dark = false;
    bool found = false;
    for (auto& s : APP_STYLES) {
        if (lower.indexOf(s.match) >= 0) {
            tint = s.tint;
            dark = s.darkGlyph;
            found = true;
            break;
        }
    }
    if (!found) {  // unbekannte Apps: stabile Farbe aus dem Namen
        static const theme::Tint FALLBACK[] = {theme::Tint::Blue, theme::Tint::Purple, theme::Tint::Teal,
                                               theme::Tint::Orange, theme::Tint::Indigo, theme::Tint::Pink};
        uint32_t h = 0;
        for (size_t i = 0; i < app.length(); i++) h = h * 31 + uint8_t(app[i]);
        tint = FALLBACK[h % 6];
    }
    char letter[8] = "?";
    if (app.length()) {
        // erstes Zeichen inkl. UTF-8-Folgebytes
        size_t n = 1;
        while (n < app.length() && n < 4 && (uint8_t(app[n]) & 0xC0) == 0x80) n++;
        memcpy(letter, app.c_str(), n);
        letter[n] = 0;
    }
    lv_obj_t* b = theme::appIcon(parent, letter, theme::tint(tint), size, dark);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), size >= 36 ? &font_title : &font_headline, 0);
    return b;
}

void MessagesApp::formatAge(char* buf, size_t len, uint32_t at) {
    time_t now = time(nullptr);
    if (!rtc_clock::isValid() || at == 0 || now < time_t(at)) {
        snprintf(buf, len, "jetzt");
        return;
    }
    uint32_t diff = now - at;
    if (diff < 60) {
        snprintf(buf, len, "jetzt");
    } else if (diff < 3600) {
        snprintf(buf, len, "vor %lu" UI_NNBSP "min", (unsigned long)(diff / 60));
    } else {
        time_t t = at;
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(buf, len, "%02d:%02d", lt.tm_hour, lt.tm_min);
    }
}

void MessagesApp::create(lv_obj_t* tile) {
    lv_obj_t* cap = theme::label(tile, theme::textCaption(), "MITTEILUNGEN");
    lv_obj_set_style_text_color(cap, theme::tint(theme::Tint::Blue), 0);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_set_pos(cap, 18, 12);
    m_count = theme::label(tile, theme::textHeadline(), "");
    lv_obj_set_pos(m_count, 18, 30);

    m_list = lv_obj_create(tile);
    lv_obj_remove_style_all(m_list);
    lv_obj_set_size(m_list, 240, 240 - 58);
    lv_obj_set_pos(m_list, 0, 58);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(m_list, 10, 0);
    lv_obj_set_style_pad_bottom(m_list, 14, 0);
    lv_obj_set_style_pad_row(m_list, 6, 0);
    lv_obj_set_scroll_dir(m_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_list, LV_SCROLLBAR_MODE_OFF);

    m_empty = ui::emptyState(tile, ICON_BELL, theme::tint(theme::Tint::Red), "Keine Mitteilungen",
                             "Neue Mitteilungen vom Handy erscheinen hier");

    store::subscribe(Topic::Messages, [this] { rebuild(); });
    store::subscribe(Topic::Connection, [this] { rebuild(); });
    rebuild();
}

void MessagesApp::rebuild() {
    const WatchData& d = store::data();
    lv_obj_clean(m_list);

    char buf[40];
    if (d.messages.empty()) {
        ui::setText(m_count, "");
        lv_obj_add_flag(m_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
        ui::setText(lv_obj_get_child(m_empty, 2),
                    d.bleConnected ? "Neue Mitteilungen vom Handy erscheinen hier" : "Handy nicht verbunden");
        return;
    }
    lv_obj_add_flag(m_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_list, LV_OBJ_FLAG_HIDDEN);
    snprintf(buf, sizeof(buf), d.messages.size() == 1 ? "1 neue" : "%u neue", unsigned(d.messages.size()));
    ui::setText(m_count, buf);

    for (const PhoneMessage& m : d.messages) {
        lv_obj_t* card = theme::cardObj(m_list);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_pad_all(card, 10, 0);

        appBadge(card, m.app, 24);
        lv_obj_t* app = theme::label(card, theme::textCaption(), m.app.c_str());
        lv_label_set_long_mode(app, LV_LABEL_LONG_DOT);
        lv_obj_set_width(app, 110);
        lv_obj_set_pos(app, 32, 2);
        formatAge(buf, sizeof(buf), m.receivedAt);
        lv_obj_t* age = theme::label(card, theme::textCaption(), buf);
        lv_obj_align(age, LV_ALIGN_TOP_RIGHT, 0, 2);

        lv_obj_t* title = theme::label(card, theme::textHeadline(), m.title.c_str());
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title, LV_PCT(100));
        lv_obj_set_pos(title, 0, 30);

        lv_obj_t* body = theme::label(card, theme::textBody(), m.body.c_str());
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        lv_obj_set_size(body, LV_PCT(100), 44);  // max. 2 Zeilen
        lv_obj_set_pos(body, 0, 52);
        if (!m.body.length()) lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);

        // Key in einem eigenen String halten (Liste kann sich ändern)
        auto* key = new String(m.key);
        lv_obj_add_event_cb(
            card, [](lv_event_t* e) { MessagesApp::openDetail(*static_cast<String*>(lv_event_get_user_data(e))); },
            LV_EVENT_CLICKED, key);
        lv_obj_add_event_cb(
            card, [](lv_event_t* e) { delete static_cast<String*>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
            key);
    }
}

void MessagesApp::openDetail(const String& key) {
    const PhoneMessage* m = findMessage(key);
    if (!m) return;

    ui::Sheet s = ui::openSheet(m->app.c_str());
    char buf[32];
    formatAge(buf, sizeof(buf), m->receivedAt);
    theme::label(s.content, theme::textCaption(), buf);

    lv_obj_t* title = theme::label(s.content, theme::textTitle(), m->title.c_str());
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);

    lv_obj_t* body = theme::label(s.content, theme::textBody(), m->body.c_str());
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    lv_obj_t* del = theme::pillButton(s.content, LV_SYMBOL_TRASH "  Löschen", false);
    lv_obj_set_width(del, LV_PCT(100));
    lv_obj_set_style_text_color(del, theme::colors().red, 0);
    lv_obj_set_style_pad_top(del, 12, 0);
    auto* k = new String(key);
    lv_obj_add_event_cb(
        del,
        [](lv_event_t* e) {
            String key = *static_cast<String*>(lv_event_get_user_data(e));
            haptics::tap();
            ui::closeAllSheets();
            store::dismissMessage(key);
        },
        LV_EVENT_CLICKED, k);
    lv_obj_add_event_cb(
        del, [](lv_event_t* e) { delete static_cast<String*>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, k);
}
