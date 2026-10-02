#pragma once

#include <string>
#include <vector>

#include "go/board.h"
#include "go/game.h"

namespace vgo {

// A variation is an independent "try-out" board (试下模式) that never mutates the
// real game: it starts from a board snapshot and replays a list of moves. The
// user plays and undoes moves freely — there is no per-step confirmation, and
// undo simply removes the last step, so the visible last move is always the
// actual last step. Pure data + a minimal controller, no Qt (N2).
struct VariationStep {
    Move move;
    Color color = Color::Black;   // who plays this step
    bool by_ai = false;           // true if an engine chose this move
};

struct Variation {
    Board start;                  // board at demo start
    Color to_move = Color::Black;
    std::vector<VariationStep> steps;

    // Board after replaying the first `reveal` steps.
    Board boardAt(int reveal) const;
};

class VariationController {
public:
    void start(const Board& start, Color to_move);
    void commitStep(const Move& m, Color color, bool by_ai);
    bool back();       // remove the last step; false when already empty
    void exit();

    bool active() const { return active_; }
    const Variation& variation() const { return v_; }
    Board board() const { return v_.boardAt(static_cast<int>(v_.steps.size())); }
    Color toMove() const;
    const VariationStep* lastStep() const;  // steps.back(), or nullptr

private:
    Variation v_;
    bool active_ = false;
};

}  // namespace vgo
