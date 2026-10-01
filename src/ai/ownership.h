#pragma once

#include <vector>

#include "analysis.h"
#include "go/board.h"

namespace vgo {

// A stone is "dead" when ownership confidently assigns it to the opponent:
// the stone's color disagrees with the ownership sign AND certainty is high
// (|ownership| > threshold). Pure function (M6.3). Default threshold 0.8.
std::vector<Point> deadStones(const Board& board, const OwnershipMap& ownership,
                              double threshold = 0.8);

// Estimated black score lead in points from ownership (expected black territory
// minus white territory, komi NOT applied). KataGo's ownership already folds in
// dead stones, so summing it is a valid territory estimate (M6.2).
double ownershipLead(const OwnershipMap& ownership);

}  // namespace vgo
