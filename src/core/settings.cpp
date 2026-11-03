#include "settings.h"

#include <Preferences.h>

namespace settings {

static Preferences prefs;

static constexpr const char* NS             = "watch";
static constexpr const char* KEY_FACE       = "face";
static constexpr const char* KEY_DARK       = "dark";
static constexpr const char* KEY_BRIGHTNESS = "bright";

void begin() { prefs.begin(NS, false); }

uint8_t getU8(const char* key, uint8_t def) { return prefs.getUChar(key, def); }

void putU8(const char* key, uint8_t value) {
    if (prefs.isKey(key) && prefs.getUChar(key) == value) return;
    prefs.putUChar(key, value);
}

String getString(const char* key, const String& def) { return prefs.getString(key, def); }

void putString(const char* key, const String& value) {
    if (prefs.isKey(key) && prefs.getString(key) == value) return;
    prefs.putString(key, value);
}

uint8_t lastWatchface()               { return getU8(KEY_FACE, 0); }
void    setLastWatchface(uint8_t row) { putU8(KEY_FACE, row); }

bool darkMode()          { return getU8(KEY_DARK, 1) != 0; }
void setDarkMode(bool d) { putU8(KEY_DARK, d ? 1 : 0); }

uint8_t brightness()                { return getU8(KEY_BRIGHTNESS, 160); }
void    setBrightness(uint8_t level) { putU8(KEY_BRIGHTNESS, level); }

}  // namespace settings
