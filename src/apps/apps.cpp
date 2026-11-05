#include "apps.h"

#include "apps/hop_tower_app.h"
#include "apps/ir_remote_app.h"
#include "apps/media_app.h"
#include "apps/messages_app.h"
#include "apps/notes_app.h"
#include "apps/placeholder_app.h"
#include "apps/runner_app.h"
#include "apps/stopwatch_app.h"
#include "apps/tictactoe_app.h"
#include "apps/untis_app.h"
#include "apps/watchfaces.h"
#include "ui/ui_core.h"

namespace apps {

// --- Platzhalter (werden Schritt für Schritt durch echte Apps ersetzt) -------
using theme::Tint;
static HopTowerApp  s_hopTower;
static TicTacToeApp s_ticTacToe;
static RunnerApp    s_runner;

static UntisApp    s_untis;
static MediaApp    s_spotify;
static MessagesApp s_messages;

static StopwatchApp s_stopwatch;
static NotesApp s_notes;
static PlaceholderApp s_shortcut2("Kurzbefehl 2", ICON_BOLT, Tint::Pink);

static IrRemoteApp s_irRemote;
static PlaceholderApp s_hack("Hack", ICON_TERMINAL, Tint::Gray);

void registerAll() {
    // X=0  Games
    ui::registerApp(0, 0, &s_hopTower);
    ui::registerApp(0, 1, &s_ticTacToe);
    ui::registerApp(0, 2, &s_runner);

    // X=1  Watchfaces (Start-Spalte)
    watchfaces::registerColumn(ui::COL_WATCHFACES);

    // X=2  Media & Productivity
    ui::registerApp(2, 0, &s_untis);
    ui::registerApp(2, 1, &s_spotify);
    ui::registerApp(2, 2, &s_messages);

    // X=3  Widgets & Notes
    ui::registerApp(3, 0, &s_stopwatch);
    ui::registerApp(3, 1, &s_notes);
    ui::registerApp(3, 2, &s_shortcut2);

    // X=4  Tools & Hacks
    ui::registerApp(4, 0, &s_irRemote);
    ui::registerApp(4, 1, &s_hack);
}

}  // namespace apps
