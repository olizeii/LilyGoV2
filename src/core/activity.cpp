#include "activity.h"

#include "core/data_store.h"
#include "core/settings.h"
#include "hal/haptics.h"
#include "hal/motion.h"
#include "hal/rtc_clock.h"

namespace activity {

static constexpr const char* KEY_DAY   = "act_day";
static constexpr const char* KEY_STEPS = "act_steps";
static constexpr const char* KEY_GOAL  = "act_goal";

static constexpr uint32_t POLL_MS        = 1000;
static constexpr uint32_t SAVE_MS        = 5UL * 60UL * 1000UL;
static constexpr uint32_t STRIDE_MM      = 750;
static constexpr uint32_t DEFAULT_GOAL   = 10000;

static uint32_t s_day      = 0;  // YYYYDDD, 0 = Datum unbekannt
static uint32_t s_base     = 0;  // Schritte vor dem letzten Sensor-Reset
static uint32_t s_steps    = 0;
static uint32_t s_saved    = 0;
static uint32_t s_lastPoll = 0;
static uint32_t s_lastSave = 0;
static uint32_t s_goal     = DEFAULT_GOAL;

static uint32_t today() {
    if (!rtc_clock::isValid()) return 0;
    struct tm t = rtc_clock::now();
    return uint32_t(t.tm_year + 1900) * 1000 + t.tm_yday;
}

static void save() {
    settings::putU32(KEY_DAY, s_day);
    settings::putU32(KEY_STEPS, s_steps);
    s_saved    = s_steps;
    s_lastSave = millis();
}

void flush() {
    if (s_steps != s_saved) save();
}

void begin() {
    motion::begin();
    s_goal = settings::getU32(KEY_GOAL, DEFAULT_GOAL);
    s_day = today();
    if (s_day != 0 && settings::getU32(KEY_DAY, 0) == s_day) {
        s_base = settings::getU32(KEY_STEPS, 0);  // Neustart am selben Tag
    }
    s_steps = s_saved = s_base;
    store::publish(Topic::Activity);
}

void loop() {
    if (!motion::isOk() || millis() - s_lastPoll < POLL_MS) return;
    s_lastPoll = millis();

    uint32_t d = today();
    if (d != 0 && d != s_day) {
        if (s_day == 0) {
            s_day = d;  // Uhrzeit kam erst später (BLE) -> bisherige Schritte gehören zu heute
        } else {
            s_day  = d;  // Mitternacht: neuer Tag
            s_base = 0;
            motion::resetSteps();
        }
        s_steps = s_base + motion::stepCount();
        save();
        store::publish(Topic::Activity);
        return;
    }

    uint32_t now = s_base + motion::stepCount();
    if (now != s_steps) {
        uint32_t g = goal();
        if (s_steps < g && now >= g) haptics::success();  // Tagesziel erreicht
        s_steps = now;
        store::publish(Topic::Activity);
    }
    if (s_steps != s_saved && millis() - s_lastSave >= SAVE_MS) save();
}

bool     available() { return motion::isOk(); }
uint32_t steps()     { return s_steps; }
uint32_t goal()      { return s_goal; }
uint32_t distanceM() { return steps() * STRIDE_MM / 1000; }
uint32_t kcal()      { return steps() * 4 / 100; }

}  // namespace activity
