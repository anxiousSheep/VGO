#include "ownership.h"

#include <cmath>

namespace vgo {

std::vector<Point> deadStones(const Board& board, const OwnershipMap& ownership,
                              double threshold) {
    std::vector<Point> out;
    if (ownership.empty() || ownership.size != board.size()) return out;
    for (int y = 0; y < board.size(); ++y) {
        for (int x = 0; x < board.size(); ++x) {
            const Color c = board.at(x, y);
            if (c == Color::Empty) continue;
            const double o = ownership.at(x, y);
            if (std::abs(o) <= threshold) continue;
            const bool stoneBlack = (c == Color::Black);
            const bool ownerBlack = (o > 0.0);
            // A stone is dead when it sits in confidently-opponent territory.
            if (stoneBlack != ownerBlack) out.push_back({x, y});
        }
    }
    return out;
}

double ownershipLead(const OwnershipMap& ownership) {
    double lead = 0.0;
    for (double v : ownership.value) lead += v;
    return lead;
}

}  // namespace vgo
