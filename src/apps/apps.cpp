#include "apps.h"

#include "apps/placeholder_app.h"
#include "apps/runner_app.h"
#include "apps/tictactoe_app.h"
#include "apps/watchfaces.h"
#include "ui/ui_core.h"

namespace apps {

// --- Platzhalter (werden Schritt für Schritt durch echte Apps ersetzt) -------
using theme::Tint;
static TicTacToeApp s_ticTacToe;
static RunnerApp    s_runner;

static PlaceholderApp s_untis("Stundenplan", ICON_CALENDAR, Tint::Red);
static PlaceholderApp s_spotify("Spotify", ICON_SPOTIFY, Tint::Spotify, true);
static PlaceholderApp s_messages("Mitteilungen", ICON_CHAT, Tint::Blue);

static PlaceholderApp s_shortcut1("Kurzbefehl 1", ICON_BOLT, Tint::Purple);
static PlaceholderApp s_notes("Notizen", ICON_PEN, Tint::Yellow, true);
static PlaceholderApp s_shortcut2("Kurzbefehl 2", ICON_BOLT, Tint::Pink);

static PlaceholderApp s_irRemote("Fernbedienung", ICON_TV, Tint::Teal);
static PlaceholderApp s_hack("Hack", ICON_TERMINAL, Tint::Gray);

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
