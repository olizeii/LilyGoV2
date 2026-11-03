#include "tictactoe_app.h"

#include <esp_random.h>

#include "hal/haptics.h"
#include "ui/theme.h"

namespace {

constexpr lv_coord_t CELL     = 56;
constexpr lv_coord_t CELL_GAP = 6;
constexpr lv_coord_t BOARD    = 3 * CELL + 2 * CELL_GAP;  // 180
constexpr uint32_t AI_DELAY   = 450;   // ms – wirkt natürlicher als sofort
constexpr uint32_t AI_MISTAKE = 25;    // % zufälliger Zug

const int LINES[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6},
                         {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

lv_color_t playerColor() { return theme::colors().accent; }
lv_color_t aiColor()     { return theme::colors().accentAlt; }

}  // namespace

// -----------------------------------------------------------------------------
//  Aufbau
// -----------------------------------------------------------------------------
void TicTacToeApp::create(lv_obj_t* tile) {
    m_status = theme::label(tile, theme::textHeadline(), "");
    lv_obj_align(m_status, LV_ALIGN_TOP_MID, 0, 8);
    m_sub = theme::label(tile, theme::textCaption(), "");
    lv_obj_align(m_sub, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t* board = lv_obj_create(tile);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, BOARD, BOARD);
    lv_obj_align(board, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 9; i++) {
        lv_obj_t* c = theme::cardObj(board);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(c, 14, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_size(c, CELL, CELL);
        lv_obj_set_pos(c, (i % 3) * (CELL + CELL_GAP), (i / 3) * (CELL + CELL_GAP));
        lv_obj_add_event_cb(
            c,
            [](lv_event_t* e) {
                auto* self = static_cast<TicTacToeApp*>(lv_event_get_user_data(e));
                lv_obj_t* cell = lv_event_get_target(e);
                for (int k = 0; k < 9; k++)
                    if (self->m_cells[k] == cell) self->onCellClicked(k);
            },
            LV_EVENT_CLICKED, this);
        m_cells[i] = c;
    }

    theme::onChange([this] { refreshColors(); });
    newRound();
}

void TicTacToeApp::onShow() {
    if (!m_over && !m_playerTurn) scheduleAi();  // angefangenen KI-Zug fortsetzen
}

void TicTacToeApp::onHide() {
    // KI-Zug nicht im Hintergrund ausführen – onShow() setzt ihn fort
    if (m_aiTimer) {
        lv_timer_del(m_aiTimer);
        m_aiTimer = nullptr;
    }
}

// -----------------------------------------------------------------------------
//  Spielablauf
// -----------------------------------------------------------------------------
void TicTacToeApp::newRound() {
    for (int i = 0; i < 9; i++) {
        m_board[i] = EMPTY;
        lv_obj_clean(m_cells[i]);
        lv_obj_remove_local_style_prop(m_cells[i], LV_STYLE_BG_COLOR, 0);
    }
    m_over       = false;
    m_playerTurn = m_playerStarts;
    m_playerStarts = !m_playerStarts;  // nächste Runde beginnt der andere
    updateStatus();
    if (!m_playerTurn) scheduleAi();
}

void TicTacToeApp::onCellClicked(int idx) {
    if (m_over) {
        newRound();
        return;
    }
    if (!m_playerTurn || m_board[idx] != EMPTY) return;
    haptics::tap();
    place(idx, PLAYER);
    m_playerTurn = false;
    afterMove();
}

void TicTacToeApp::place(int idx, Cell who) {
    m_board[idx] = who;
    lv_obj_t* cell = m_cells[idx];

    if (who == PLAYER) {
        lv_obj_t* x = lv_label_create(cell);
        lv_label_set_text(x, ICON_CLOSE);
        lv_obj_set_style_text_font(x, &font_icon, 0);
        lv_obj_set_style_text_color(x, playerColor(), 0);
        lv_obj_center(x);
    } else {
        lv_obj_t* o = lv_obj_create(cell);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, 30, 30);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(o, 6, 0);
        lv_obj_set_style_border_color(o, aiColor(), 0);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(o);
    }
    lv_obj_fade_in(lv_obj_get_child(cell, 0), 150, 0);
}

void TicTacToeApp::afterMove() {
    int line[3];
    int w = winner(m_board, line);
    if (w != 0) {
        showResult(Cell(w), w == 3 ? nullptr : line);
        return;
    }
    updateStatus();
    if (!m_playerTurn) scheduleAi();
}

void TicTacToeApp::scheduleAi() {
    if (m_aiTimer) return;
    m_aiTimer = lv_timer_create(
        [](lv_timer_t* t) {
            auto* self = static_cast<TicTacToeApp*>(t->user_data);
            self->m_aiTimer = nullptr;
            lv_timer_del(t);
            self->aiMove();
        },
        AI_DELAY, this);
}

void TicTacToeApp::aiMove() {
    if (m_over || m_playerTurn) return;
    int move = -1;
    if (esp_random() % 100 < AI_MISTAKE) {
        int free[9], n = 0;
        for (int i = 0; i < 9; i++)
            if (m_board[i] == EMPTY) free[n++] = i;
        if (n) move = free[esp_random() % n];
    } else {
        move = bestMove();
    }
    if (move < 0) return;
    place(move, AI);
    m_playerTurn = true;
    afterMove();
}

void TicTacToeApp::showResult(Cell w, const int* line) {
    m_over = true;
    if (w == PLAYER) {
        m_scorePlayer++;
        lv_label_set_text(m_status, "Gewonnen!");
        haptics::success();
    } else if (w == AI) {
        m_scoreAi++;
        lv_label_set_text(m_status, "Verloren");
        haptics::failure();
    } else {
        m_draws++;
        lv_label_set_text(m_status, "Unentschieden");
    }
    lv_label_set_text(m_sub, "Tippen für neue Runde");

    if (line) {
        lv_color_t c = w == PLAYER ? playerColor() : aiColor();
        lv_color_t bg = lv_color_mix(c, theme::colors().surface, LV_OPA_30);
        for (int k = 0; k < 3; k++) lv_obj_set_style_bg_color(m_cells[line[k]], bg, 0);
    }
}

void TicTacToeApp::updateStatus() {
    lv_label_set_text(m_status, m_playerTurn ? "Du bist dran" : "Uhr überlegt …");
    lv_label_set_text_fmt(m_sub, "Du %u  ·  Uhr %u", m_scorePlayer, m_scoreAi);
}

void TicTacToeApp::refreshColors() {
    for (int i = 0; i < 9; i++) {
        lv_obj_t* mark = lv_obj_get_child(m_cells[i], 0);
        if (!mark) continue;
        if (m_board[i] == PLAYER) lv_obj_set_style_text_color(mark, playerColor(), 0);
        else                      lv_obj_set_style_border_color(mark, aiColor(), 0);
    }
    if (m_over) {  // Gewinnlinie neu einfärben
        int line[3];
        int w = winner(m_board, line);
        for (int i = 0; i < 9; i++) lv_obj_remove_local_style_prop(m_cells[i], LV_STYLE_BG_COLOR, 0);
        if (w == PLAYER || w == AI) {
            lv_color_t c  = w == PLAYER ? playerColor() : aiColor();
            lv_color_t bg = lv_color_mix(c, theme::colors().surface, LV_OPA_30);
            for (int k = 0; k < 3; k++) lv_obj_set_style_bg_color(m_cells[line[k]], bg, 0);
        }
    }
}

// -----------------------------------------------------------------------------
//  KI (Minimax – bei 9 Feldern trivial schnell)
// -----------------------------------------------------------------------------
// 0 = läuft noch, 1 = Spieler, 2 = KI, 3 = unentschieden
int TicTacToeApp::winner(const Cell* b, int* lineOut) {
    for (auto& l : LINES) {
        if (b[l[0]] != EMPTY && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]]) {
            if (lineOut) memcpy(lineOut, l, sizeof(l));
            return b[l[0]];
        }
    }
    for (int i = 0; i < 9; i++)
        if (b[i] == EMPTY) return 0;
    return 3;
}

int TicTacToeApp::minimax(Cell* b, bool aiTurn, int depth) {
    int w = winner(b);
    if (w == AI)     return 10 - depth;
    if (w == PLAYER) return depth - 10;
    if (w == 3)      return 0;

    int best = aiTurn ? -100 : 100;
    for (int i = 0; i < 9; i++) {
        if (b[i] != EMPTY) continue;
        b[i] = aiTurn ? AI : PLAYER;
        int score = minimax(b, !aiTurn, depth + 1);
        b[i] = EMPTY;
        best = aiTurn ? max(best, score) : min(best, score);
    }
    return best;
}

int TicTacToeApp::bestMove() {
    int best = -100, candidates[9], n = 0;
    for (int i = 0; i < 9; i++) {
        if (m_board[i] != EMPTY) continue;
        m_board[i] = AI;
        int score = minimax(m_board, false, 1);
        m_board[i] = EMPTY;
        if (score > best) {
            best = score;
            n = 0;
        }
        if (score == best) candidates[n++] = i;
    }
    return n ? candidates[esp_random() % n] : -1;  // gleichwertige Züge variieren
}
