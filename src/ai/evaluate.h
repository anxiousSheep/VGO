#pragma once

#include <functional>
#include <string>

#include "analysis.h"
#include "engine.h"
#include "explain.h"
#include "go/game.h"
#include "strength.h"

namespace vgo {

struct PointMatch {
    bool found = false;
    Point p{-1, -1};
};

// Find the first GTP coordinate in free text (e.g. "G3", "下 G3", "g3 怎么样").
// `size` is the board size, used to bound the coordinate range.
PointMatch findPointInQuestion(const std::string& text, int size);

// Pure: fill mover-perspective winrate/lead before/after from two analyses.
MoveEvaluation computeMoveEvaluation(const MoveEvaluation& in,
                                     const Analysis& before, Color before_to_move,
                                     const Analysis& after, Color after_to_move,
                                     Color mover);

// Async what-if: play `p` (must be a legal move for `game`) on a board copy,
// re-analyze both positions, and report the delta. Without an engine the result
// is legal-but-invalid (no quantification). Uses a fixed analysis strength.
class MoveEvaluator {
public:
    using Callback = std::function<void(const MoveEvaluation&)>;

    explicit MoveEvaluator(EngineInterface* engine);
    void evaluate(const Game& game, const Point& p, Callback cb);
    void cancel();

private:
    EngineInterface* engine_;
    StrengthParams strength_;
    int generation_ = 0;
};

// Whether a move is a blunder by winrate/lead drop (mover perspective).
bool isBlunder(const MoveEvaluation& e, double winrate_drop = 0.15,
               double lead_drop = 3.0);

// Chinese warning text if the move is a blunder, empty otherwise.
std::string blunderWarning(const MoveEvaluation& e);

}  // namespace vgo
