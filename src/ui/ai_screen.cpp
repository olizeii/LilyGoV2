#include "ai_screen.h"

#include "apps/ir_remote_app.h"
#include "core/data_store.h"
#include "core/settings.h"
#include "core/voice.h"
#include "hal/display.h"
#include "hal/haptics.h"
#include "ui/map_screen.h"
#include "ui/theme.h"
#include "ui/ui_core.h"
#include "ui/widgets.h"

namespace aiscreen {
namespace {

enum class State : uint8_t { Listening, Thinking, Answer, Error };

constexpr uint32_t FRAME_MS     = 33;
constexpr uint32_t ACK_TIMEOUT  = 4000;    // WatchBridge muss AI_START bestätigen
constexpr uint32_t REPLY_TIMEOUT = 45000;  // so lange auf eine Antwort warten
constexpr size_t MAX_ANSWER     = 2400;
constexpr int BLOBS             = 3;
constexpr lv_coord_t ORB_Y      = -14;

const uint32_t BLOB_COLORS[BLOBS] = {0x0A84FF, 0xFF375F, 0x64D2FF};
const float BLOB_W[BLOBS][2]      = {{1.9f, 1.3f}, {1.4f, 2.1f}, {2.4f, 1.7f}};  // Bahn-Frequenzen

lv_obj_t* s_root   = nullptr;
lv_obj_t* s_orb    = nullptr;  // Container der Kugel
lv_obj_t* s_halo[3] = {};
lv_obj_t* s_blob[BLOBS] = {};
lv_obj_t* s_core   = nullptr;
lv_obj_t* s_status = nullptr;  // "Ich höre zu …" bzw. Mitschrift
lv_obj_t* s_scroll = nullptr;  // Antwortansicht
lv_obj_t* s_heardL = nullptr;
lv_obj_t* s_answerL = nullptr;
lv_obj_t* s_again  = nullptr;  // Mikro-Knopf "nochmal fragen"
lv_timer_t* s_timer = nullptr;
lv_timer_t* s_alarm = nullptr; // Timer-Aktion ("Stell einen Timer auf 5 Minuten")
String s_alarmTitle;

State s_state     = State::Listening;
String s_heard;
String s_answer;
bool s_acked      = false;
bool s_done       = true;
uint32_t s_startMs = 0;
uint32_t s_lastRx  = 0;
float s_t         = 0;     // Animationszeit
float s_lvl       = 0;     // geglätteter Pegel für die Kugel

lv_obj_t* circle(lv_obj_t* parent, lv_coord_t size, uint32_t color, lv_opa_t opa) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(c, size, size);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(c, opa, 0);
    lv_obj_center(c);
    return c;
}

void placeCircle(lv_obj_t* c, lv_coord_t size, float dx, float dy) {
    lv_obj_set_size(c, size, size);
    lv_obj_align(c, LV_ALIGN_CENTER, lv_coord_t(dx), lv_coord_t(dy));
}

// --- Darstellung --------------------------------------------------------------------

void showOrb(bool visible) {
    if (visible) {
        lv_obj_clear_flag(s_orb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_orb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
    }
}

void setStatus(const char* text, uint32_t color = 0x8E8E93) {
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    ui::setText(s_status, text);
}

void setAgainVisible(bool v) {
    if (v) lv_obj_clear_flag(s_again, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(s_again, LV_OBJ_FLAG_HIDDEN);
}

void enter(State st) {
    s_state = st;
    switch (st) {
        case State::Listening:
            showOrb(true);
            setAgainVisible(false);
            setStatus("Ich h\xC3\xB6re zu \xE2\x80\xA6");
            break;
        case State::Thinking:
            voice::stop();
            showOrb(true);
            setAgainVisible(false);
            if (!s_heard.length()) setStatus("Einen Moment \xE2\x80\xA6");
            break;
        case State::Answer:
            voice::stop();
            showOrb(false);
            ui::setText(s_heardL, s_heard.c_str());
            if (s_heard.length()) lv_obj_clear_flag(s_heardL, LV_OBJ_FLAG_HIDDEN);
            else                  lv_obj_add_flag(s_heardL, LV_OBJ_FLAG_HIDDEN);
            lv_obj_scroll_to_y(s_scroll, 0, LV_ANIM_OFF);
            setAgainVisible(false);
            break;
        case State::Error:
            voice::stop();
            showOrb(true);
            setAgainVisible(true);
            break;
    }
}

void showError(const char* msg) {
    s_done = true;
    enter(State::Error);
    setStatus(msg, 0xFF453A);
    haptics::failure();
}

void listen() {
    s_heard  = "";
    s_answer = "";
    ui::setText(s_answerL, "");
    s_acked  = false;
    s_done   = false;
    s_startMs = s_lastRx = millis();
    enter(State::Listening);

    if (!store::data().bleConnected) {
        showError("Handy nicht verbunden");
        return;
    }
    store::sendToPhone("AI_START", String(voice::SAMPLE_RATE));
    if (!voice::start()) {
        store::sendToPhone("AI_CANCEL");
        showError("Mikrofon nicht verf\xC3\xBCgbar");
    }
}

// Kugel: Halo-Ringe + drei additiv überlagerte Farbkugeln auf Lissajous-Bahnen
void animate() {
    float target = 0;
    float speed  = 1.0f;
    switch (s_state) {
        case State::Listening: target = voice::level(); speed = 1.0f + s_lvl * 1.5f; break;
        case State::Thinking:  target = 0.25f + 0.15f * sinf(s_t * 3.0f); speed = 2.6f; break;
        case State::Error:     target = 0; speed = 0.3f; break;
        case State::Answer:    return;
    }
    s_lvl += (target - s_lvl) * 0.35f;
    s_t += FRAME_MS / 1000.0f * speed;

    float breathe = sinf(s_t * 1.6f);
    lv_coord_t base = lv_coord_t(78 + 6 * breathe + 34 * s_lvl);
    for (int i = 0; i < 3; i++) {
        lv_coord_t hs = lv_coord_t(base + 26 + i * 18 + 10 * s_lvl * (i + 1));
        placeCircle(s_halo[i], hs, 0, 0);
    }
    float r = 8 + 16 * s_lvl;
    for (int i = 0; i < BLOBS; i++) {
        float ph = i * 2.094f;
        float dx = r * cosf(s_t * BLOB_W[i][0] + ph);
        float dy = r * sinf(s_t * BLOB_W[i][1] + ph * 1.3f);
        lv_coord_t sz = lv_coord_t(base - 6 + 8 * sinf(s_t * 2.3f + ph));
        placeCircle(s_blob[i], sz, dx, dy);
    }
    placeCircle(s_core, lv_coord_t(base * 0.45f), 0, 0);
    lv_obj_set_style_bg_opa(s_core, lv_opa_t(70 + 120 * s_lvl), 0);

    bool grey = s_state == State::Error;
    for (int i = 0; i < BLOBS; i++)
        lv_obj_set_style_bg_color(s_blob[i], grey ? lv_color_hex(0x3A3A3C) : lv_color_hex(BLOB_COLORS[i]), 0);
}

void tick(lv_timer_t*) {
    if (!s_root) return;
    animate();
    uint32_t now = millis();

    if (s_state == State::Listening) {
        if (!s_acked && now - s_startMs > ACK_TIMEOUT) {
            store::sendToPhone("AI_CANCEL");
            showError("WatchBridge antwortet nicht");
            return;
        }
        if (!voice::isRunning() && s_acked) enter(State::Thinking);  // max. Aufnahmedauer erreicht
    }
    if (!s_done && (s_state == State::Thinking || s_state == State::Answer) && now - s_lastRx > REPLY_TIMEOUT) {
        store::sendToPhone("AI_CANCEL");
        showError("Keine Antwort erhalten");
        return;
    }
    // Während zugehört / gedacht / geschrieben wird, nicht einschlafen
    if (!s_done) lv_disp_trig_activity(nullptr);
}

void onAgain(lv_event_t*) {
    haptics::tap();
    listen();
}

void build() {
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);

    // --- Kugel ---
    s_orb = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_orb);
    lv_obj_set_size(s_orb, 200, 200);
    lv_obj_align(s_orb, LV_ALIGN_CENTER, 0, ORB_Y);
    lv_obj_clear_flag(s_orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_orb, LV_OBJ_FLAG_CLICKABLE);
    const lv_opa_t haloOpa[3] = {46, 26, 14};
    for (int i = 0; i < 3; i++) s_halo[i] = circle(s_orb, 100, 0x5E5CE6, haloOpa[i]);
    for (int i = 0; i < BLOBS; i++) {
        s_blob[i] = circle(s_orb, 80, BLOB_COLORS[i], 190);
        lv_obj_set_style_blend_mode(s_blob[i], LV_BLEND_MODE_ADDITIVE, 0);
    }
    s_core = circle(s_orb, 36, 0xFFFFFF, 90);
    lv_obj_set_style_blend_mode(s_core, LV_BLEND_MODE_ADDITIVE, 0);
    // Tippen auf die Kugel: fertig gesprochen (bzw. nach Fehler neu starten)
    lv_obj_add_event_cb(
        s_orb,
        [](lv_event_t*) {
            if (s_state == State::Listening) {
                haptics::tap();
                enter(State::Thinking);
            } else if (s_state == State::Error) {
                onAgain(nullptr);
            }
        },
        LV_EVENT_CLICKED, nullptr);

    s_status = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_status, &font_caption, 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status, 190);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_height(s_status, 40, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -52);

    // --- Antwort (scrollbar) ---
    s_scroll = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_scroll);
    lv_obj_set_size(s_scroll, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(s_scroll, 20, 0);
    lv_obj_set_style_pad_right(s_scroll, 20, 0);
    lv_obj_set_style_pad_top(s_scroll, 22, 0);
    lv_obj_set_style_pad_bottom(s_scroll, 64, 0);
    lv_obj_set_style_pad_row(s_scroll, 8, 0);
    lv_obj_set_flex_flow(s_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);

    // kleine Kugel als "Absender"
    lv_obj_t* mini = circle(s_scroll, 22, 0x5E5CE6, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(mini, lv_color_hex(0xFF375F), 0);
    lv_obj_set_style_bg_grad_dir(mini, LV_GRAD_DIR_VER, 0);

    s_heardL = lv_label_create(s_scroll);
    lv_obj_set_width(s_heardL, LV_PCT(100));
    lv_label_set_long_mode(s_heardL, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_heardL, &font_caption, 0);
    lv_obj_set_style_text_color(s_heardL, lv_color_hex(0x8E8E93), 0);

    s_answerL = lv_label_create(s_scroll);
    lv_obj_set_width(s_answerL, LV_PCT(100));
    lv_label_set_long_mode(s_answerL, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_answerL, &font_body, 0);
    lv_obj_set_style_text_color(s_answerL, lv_color_white(), 0);
    lv_obj_set_style_text_line_space(s_answerL, 2, 0);

    // --- Knöpfe unten ---
    lv_obj_t* x = ui::roundButton(s_root, ICON_CLOSE, 40, lv_color_hex(0x2C2C2E), lv_color_white(), &font_caption);
    lv_obj_set_ext_click_area(x, 8);
    lv_obj_align(x, LV_ALIGN_BOTTOM_LEFT, 22, -10);
    lv_obj_add_event_cb(x, [](lv_event_t*) { close(); }, LV_EVENT_CLICKED, nullptr);

    s_again = ui::roundButton(s_root, LV_SYMBOL_AUDIO, 40, lv_color_hex(0x5E5CE6), lv_color_white(), &font_caption);
    lv_obj_set_ext_click_area(s_again, 8);
    lv_obj_align(s_again, LV_ALIGN_BOTTOM_RIGHT, -22, -10);
    lv_obj_add_event_cb(s_again, onAgain, LV_EVENT_CLICKED, nullptr);
}

// --- Aktionen, die die KI auf der Uhr auslöst ---------------------------------------

void openApp(const String& app) {
    struct Target { const char* name; uint8_t col, row; };
    static const Target TARGETS[] = {
        {"hopturm", 0, 0}, {"tictactoe", 0, 1}, {"runner", 0, 2},
        {"untis", 2, 0},   {"media", 2, 1},     {"spotify", 2, 1}, {"messages", 2, 2},
        {"notes", 3, 1},   {"remote", 4, 0},    {"ir", 4, 0},
        {"activity", 1, ui::WATCHFACE_COUNT},   {"info", 1, ui::WATCHFACE_COUNT + 1},
    };
    close();
    if (app == "map") {
        mapscreen::open();
        return;
    }
    if (app == "watchface") {
        ui::showWatchface(true);
        return;
    }
    for (const auto& t : TARGETS) {
        if (app == t.name) {
            ui::showTile(t.col, t.row, true);
            return;
        }
    }
}

void startAlarm(uint32_t seconds, const String& title) {
    if (s_alarm) lv_timer_del(s_alarm);
    s_alarmTitle = title.length() ? title : String("Timer");
    s_alarm = lv_timer_create(
        [](lv_timer_t* t) {
            lv_timer_del(t);
            s_alarm = nullptr;
            // Wie eine Mitteilung: Banner + Vibration, landet in "Nachrichten"
            store::handleMessage("NOTIF|ai_timer|Timer|" + s_alarmTitle + "|Timer abgelaufen");
            haptics::failure();
        },
        seconds * 1000, nullptr);
    lv_timer_set_repeat_count(s_alarm, 1);
}

void action(const String& payload) {
    int a = payload.indexOf('|');
    String what = a < 0 ? payload : payload.substring(0, a);
    String rest = a < 0 ? "" : payload.substring(a + 1);
    int b = rest.indexOf('|');
    String arg1 = b < 0 ? rest : rest.substring(0, b);
    String arg2 = b < 0 ? "" : rest.substring(b + 1);

    if (what == "dark") {
        theme::setDark(arg1 == "1");
    } else if (what == "bright") {
        int pct = constrain(arg1.toInt(), 5, 100);
        uint8_t lvl = uint8_t(pct * 255 / 100);
        display::setBrightness(lvl);
        settings::setBrightness(lvl);
    } else if (what == "ir") {
        int n = constrain(arg2.length() ? arg2.toInt() : 1, 1, 20);
        IrRemoteApp::remote(arg1.c_str(), uint8_t(n));
    } else if (what == "open") {
        openApp(arg1);
    } else if (what == "timer") {
        startAlarm(uint32_t(constrain(arg1.toInt(), 1, 24 * 3600)), arg2);
    }
}

bool handle(const String& topic, const String& payload) {
    if (!topic.startsWith("AI_")) return false;
    if (topic == "AI_ACT") {
        action(payload);
        return true;
    }
    if (!s_root) return true;
    s_lastRx = millis();

    if (topic == "AI_STATE") {
        s_acked = true;
        if (payload == "think" && s_state == State::Listening) enter(State::Thinking);
    } else if (topic == "AI_HEARD") {
        s_heard = payload;
        if (s_state == State::Listening || s_state == State::Thinking) setStatus(s_heard.c_str(), 0xEBEBF5);
    } else if (topic == "AI_TEXT") {
        if (s_state == State::Error) return true;
        if (s_state != State::Answer) enter(State::Answer);
        String d = payload;
        d.replace("**", "");
        if (s_answer.length() + d.length() <= MAX_ANSWER) s_answer += d;
        lv_label_set_text(s_answerL, s_answer.c_str());
    } else if (topic == "AI_DONE") {
        s_done = true;
        if (s_state != State::Answer && s_state != State::Error) {
            // Nur eine Aktion ohne Text -> kurze Bestätigung
            enter(State::Answer);
            if (!s_answer.length()) lv_label_set_text(s_answerL, "Erledigt.");
        }
        setAgainVisible(true);
        haptics::tap();
    } else if (topic == "AI_ERR") {
        showError(payload.length() ? payload.c_str() : "Fehler");
    }
    return true;
}

}  // namespace

void begin() { store::addHandler(handle); }

void attach(lv_obj_t* tile) {
    lv_obj_add_event_cb(tile, [](lv_event_t*) { open(); }, LV_EVENT_LONG_PRESSED, nullptr);
}

void open() {
    if (s_root || mapscreen::isOpen()) return;
    haptics::tap();
    build();
    s_t = 0;
    s_lvl = 0;
    s_timer = lv_timer_create(tick, FRAME_MS, nullptr);
    animate();
    listen();
    lv_obj_fade_in(s_root, 160, 0);
}

void close() {
    if (!s_root) return;
    voice::stop();
    if (!s_done) store::sendToPhone("AI_CANCEL");
    s_done = true;
    if (s_timer) lv_timer_del(s_timer);
    s_timer = nullptr;
    lv_obj_del_async(s_root);
    s_root = nullptr;
}

bool isOpen() { return s_root != nullptr; }

}  // namespace aiscreen
