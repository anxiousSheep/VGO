#pragma once

#include <vector>

#include "../analysis.h"
#include "../go/board.h"
#include "../go/go_types.h"
#include "claim.h"

namespace vgo {

// Verifies extracted claims against ground truth: the Board state, the KataGo
// OwnershipMap, the Analysis (winrate / score lead), and the rule-engine score.
// Produces a three-tier disposition per claim (Pass / Corrected / Unverifiable).
// Pure and unit-testable (D2.1).
class ClaimVerifier {
public:
    struct Tolerances {
        double lead_points = 1.0;      // ± points for score-lead
        double winrate_pp = 0.05;      // ± winrate (5 percentage points)
        double lead_correctable = 15.0;  // above this a Corrected verdict is flagged as large
    };

    std::vector<ClaimVerdict> verify(const std::vector<Claim>& claims,
                                     const Board& board,
                                     const OwnershipMap& ownership,
                                     const Analysis& analysis,
                                     double score_black, Color to_move,
                                     const Tolerances& tol = {}) const;
};

}  // namespace vgo
