#include "apps.h"

#include "apps/placeholder_app.h"
#include "apps/watchfaces.h"
#include "ui/ui_core.h"

namespace apps {

// --- Platzhalter (werden Schritt für Schritt durch echte Apps ersetzt) -------
static PlaceholderApp s_ticTacToe("Tic Tac Toe", LV_SYMBOL_SHUFFLE);
static PlaceholderApp s_runner("Runner", LV_SYMBOL_PLAY);

static PlaceholderApp s_untis("Stundenplan", LV_SYMBOL_LIST);
static PlaceholderApp s_spotify("Spotify", LV_SYMBOL_AUDIO);
static PlaceholderApp s_messages("Nachrichten", LV_SYMBOL_ENVELOPE);

static PlaceholderApp s_shortcut1("Shortcut 1", LV_SYMBOL_UPLOAD);
static PlaceholderApp s_notes("Notizen", LV_SYMBOL_EDIT);
static PlaceholderApp s_shortcut2("Shortcut 2", LV_SYMBOL_UPLOAD);

static PlaceholderApp s_irRemote("IR-Remote", LV_SYMBOL_POWER);
static PlaceholderApp s_hack("Hack", LV_SYMBOL_WIFI);

void registerAll() {
    // X=0  Games
    ui::registerApp(0, 0, &s_ticTacToe);
    ui::registerApp(0, 1, &s_runner);

    // X=1  Watchfaces (Start-Spalte)
    watchfaces::registerColumn(ui::COL_WATCHFACES);

    // X=2  Media & Productivity
    ui::registerApp(2, 0, &s_untis);
    ui::registerApp(2, 1, &s_spotify);
    ui::registerApp(2, 2, &s_messages);

    // X=3  Widgets & Notes
    ui::registerApp(3, 0, &s_shortcut1);
    ui::registerApp(3, 1, &s_notes);
    ui::registerApp(3, 2, &s_shortcut2);

    // X=4  Tools & Hacks
    ui::registerApp(4, 0, &s_irRemote);
    ui::registerApp(4, 1, &s_hack);
}

}  // namespace apps
