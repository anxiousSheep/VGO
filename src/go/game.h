#pragma once

#include <string>
#include <utility>
#include <vector>

#include "board.h"
#include "go_types.h"

namespace vgo {

// Full Go rules engine: placement, capture, suicide, positional superko,
// pass, game end, area scoring (with komi), and undo. UI-agnostic (no I/O,
// no Qt).
class Game {
public:
    // Starts an empty board with Black to move.
    explicit Game(int size = 9, double komi = 7.5);

    // Loads an arbitrary position (mainly for tests / future "load game").
    explicit Game(const Board& board, Color to_move = Color::Black, double komi = 7.5);

    const Board& board() const { return history_.back().board; }
    int size() const { return board().size(); }
    Color current() const { return history_.back().current; }
    int move_number() const { return history_.back().move_number; }
    bool has_last_move() const { return history_.back().has_last_move; }
    Point last_move() const { return history_.back().last_move; }
    bool is_over() const { return history_.back().is_over; }
    double komi() const { return komi_; }

    // Stones captured by the given color so far.
    int captures(Color c) const { return history_.back().captures[static_cast<int>(c)]; }

    // True if the current player may place a stone at (x, y).
    bool is_legal(int x, int y) const;
    // Applies the move if legal; returns false and leaves state unchanged otherwise.
    bool play(int x, int y);
    // Passes the current turn. Two consecutive passes end the game.
    void pass();
    // Force-end the game (双方协商终局 or 智能裁判), without adding a move.
    void settle();
    // Undoes the last move/pass. Returns false if there is nothing to undo.
    bool undo();

    // The move sequence (color + move) played so far, in order. A pass is
    // represented by a Move with is_pass set. Used by the record / review layer.
    std::vector<std::pair<Color, Move>> moves() const;

    // A single logged move for the record layer: who played, what, whether it is
    // still on the board (undo marks it inactive instead of dropping it), and its
    // 1-based move number at the time it was played.
    struct LoggedMove {
        Color color = Color::Black;
        Move move;
        bool active = true;
        int number = 0;
    };

    // Full append-only move log (including undone moves). Unlike moves(), undo
    // does not drop entries — it flips `active` to false — so the record layer
    // can persist the undo history.
    const std::vector<LoggedMove>& moveLog() const { return move_log_; }

    // Enumerates all legal moves for the current player.
    std::vector<Point> legal_moves() const;

    // Area score (stones + surrounded territory). Does NOT include komi.
    std::pair<int, int> score_area() const;
    // Black's area minus White's area minus komi; >0 means Black leads.
    double score_black() const;
    // Winner with komi applied. Empty only on an exact tie (integer komi).
    Color winner() const;

private:
    struct State {
        Board board;
        std::string snapshot;
        Color current = Color::Black;
        int consecutive_passes = 0;
        int move_number = 0;
        bool has_last_move = false;
        Point last_move{-1, -1};
        int captures[3] = {0, 0, 0};
        bool is_over = false;
    };

    // Fills *snap_out with the resulting snapshot if the move is legal.
    bool legal_result(int x, int y, std::string* snap_out) const;

    std::vector<State> history_;
    std::vector<LoggedMove> move_log_;  // 完整着法日志（含悔棋，仅记录用）
    double komi_;
};

}  // namespace vgo
