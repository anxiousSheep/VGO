#pragma once

#include <string>
#include <vector>

#include "go/go_types.h"

namespace vgo {

// A candidate move with engine statistics.
struct MoveCandidate {
    Point p{-1, -1};
    bool is_pass = false;
    double winrate = 0.0;      // [0,1], from the mover's perspective
    double score_lead = 0.0;   // points the mover leads by (after komi)
    int visits = 0;
    std::vector<Point> pv;     // principal variation
};

// Per-point ownership estimate, -1 (white) .. +1 (black), row-major.
struct OwnershipMap {
    int size = 0;
    std::vector<double> value;
    bool empty() const { return value.empty(); }
    double at(int x, int y) const {
        if (x < 0 || y < 0 || x >= size || y >= size) return 0.0;
        return value[static_cast<std::size_t>(y) * size + x];
    }
};

// Engine analysis of a position. `valid == false` means no engine analysis is
// available (e.g. the fallback engine); only board-derived facts then apply.
struct Analysis {
    bool valid = false;
    double winrate = 0.0;
    double score_lead = 0.0;
    MoveCandidate best;
    std::vector<MoveCandidate> candidates;
    OwnershipMap ownership;
};

}  // namespace vgo
