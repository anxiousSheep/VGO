#pragma once

#include <vector>

#include "analysis.h"
#include "explain.h"
#include "go/board.h"

namespace vgo {

// Group confident dead stones (from `deadStones`) into connected components of
// the same color.
std::vector<std::vector<Point>> deadGroups(const Board& board,
                                           const OwnershipMap& ownership,
                                           double threshold = 0.8);

// Empty points adjacent to `group` (its liberties) — candidate saving moves.
std::vector<Point> savingCandidates(const Board& board, const std::vector<Point>& group);

// Deterministic life-death verdict for the largest confidently-dead group (if
// any). No engine, no LLM; pure and unit-testable (N1.3).
LifeDeathResult analyzeLifeDeath(const Board& board, const OwnershipMap& ownership,
                                 double threshold = 0.8);

}  // namespace vgo
