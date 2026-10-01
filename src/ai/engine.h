#pragma once

#include <functional>

#include "analysis.h"
#include "go/board.h"
#include "strength.h"

namespace vgo {

struct EngineResult {
    Move move;            // chosen move (is_pass set for a pass)
    Analysis analysis;    // may be invalid (fallback engine)
};

// Picks a move for `to_move`. Implementations may invoke `cb` synchronously
// (fallback) or asynchronously (KataGo). `cancel` aborts an in-flight request.
class EngineInterface {
public:
    virtual ~EngineInterface() = default;
    using Callback = std::function<void(EngineResult)>;
    using AnalysisCallback = std::function<void(const Analysis&)>;
    virtual void requestMove(const Board& board, Color to_move,
                             const StrengthParams& params, Callback cb) = 0;

    // Analyze a position (winrate / score lead / ownership). `Analysis.winrate`
    // and `Analysis.score_lead` are reported from `to_move`'s perspective. The
    // default returns an invalid Analysis (the fallback engine provides no
    // ownership); KataGo overrides it via `kata-analyze`.
    virtual void analyze(const Board& board, Color to_move,
                         const StrengthParams& params, AnalysisCallback cb) {
        cb(Analysis{});
    }

    virtual void cancel() {}
};

}  // namespace vgo
