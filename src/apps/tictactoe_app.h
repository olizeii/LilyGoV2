#pragma once
#include "ui/app.h"

// =============================================================================
//  Tic Tac Toe – Du (✕, blau) gegen die Uhr (◯, orange).
//  Die KI spielt per Minimax, greift aber zu 25 % daneben -> schlagbar.
//  Wer anfängt, wechselt jede Runde. Spielstand bleibt bis zum Neustart.
// =============================================================================
class TicTacToeApp : public WatchApp {
public:
    const char* name() const override { return "Tic Tac Toe"; }
    void create(lv_obj_t* tile) override;
    void onShow() override;
    void onHide() override;

private:
    enum Cell : uint8_t { EMPTY = 0, PLAYER = 1, AI = 2 };

    void newRound();
    void place(int idx, Cell who);
    void afterMove();
    void scheduleAi();
    void aiMove();
    void showResult(Cell winner, const int* line);
    void updateStatus();
    void refreshColors();
    void onCellClicked(int idx);

    static int winner(const Cell* b, int* lineOut = nullptr);
    static int minimax(Cell* b, bool aiTurn, int depth);
    int bestMove();

    lv_obj_t* m_cells[9]   = {};
    lv_obj_t* m_status     = nullptr;
    lv_obj_t* m_sub        = nullptr;
    lv_timer_t* m_aiTimer  = nullptr;

    Cell m_board[9]        = {};
    bool m_playerTurn      = true;
    bool m_over            = false;
    bool m_playerStarts    = true;
    uint16_t m_scorePlayer = 0;
    uint16_t m_scoreAi     = 0;
    uint16_t m_draws       = 0;
};
