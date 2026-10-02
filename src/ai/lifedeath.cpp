#include "lifedeath.h"

#include <array>
#include <utility>

#include "ownership.h"

namespace vgo {

namespace {
constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
}  // namespace

std::vector<std::vector<Point>> deadGroups(const Board& board,
                                           const OwnershipMap& ownership,
                                           double threshold) {
    std::vector<std::vector<Point>> groups;
    const std::vector<Point> dead = deadStones(board, ownership, threshold);
    const int n = board.size();
    std::vector<int> pos(static_cast<std::size_t>(n) * n, -1);
    for (std::size_t i = 0; i < dead.size(); ++i)
        pos[static_cast<std::size_t>(board.index(dead[i].x, dead[i].y))] =
            static_cast<int>(i);

    std::vector<bool> done(dead.size(), false);
    for (std::size_t i = 0; i < dead.size(); ++i) {
        if (done[i]) continue;
        const Color c = board.at(dead[i].x, dead[i].y);
        std::vector<Point> group;
        std::vector<std::size_t> stack{i};
        done[i] = true;
        while (!stack.empty()) {
            const std::size_t k = stack.back();
            stack.pop_back();
            group.push_back(dead[k]);
            for (const auto [dx, dy] : kDirs) {
                const int nx = dead[k].x + dx;
                const int ny = dead[k].y + dy;
                if (!board.in_bounds(nx, ny)) continue;
                const int idx = pos[static_cast<std::size_t>(board.index(nx, ny))];
                if (idx >= 0 && !done[static_cast<std::size_t>(idx)] &&
                    board.at(nx, ny) == c) {
                    done[static_cast<std::size_t>(idx)] = true;
                    stack.push_back(static_cast<std::size_t>(idx));
                }
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<Point> savingCandidates(const Board& board, const std::vector<Point>& group) {
    if (group.empty()) return {};
    return board.liberties(group.front().x, group.front().y);
}

LifeDeathResult analyzeLifeDeath(const Board& board, const OwnershipMap& ownership,
                                 double threshold) {
    LifeDeathResult r;
    std::vector<std::vector<Point>> groups = deadGroups(board, ownership, threshold);
    if (groups.empty()) {
        r.alive = true;
        return r;
    }
    auto largest = groups.begin();
    for (auto it = groups.begin(); it != groups.end(); ++it)
        if (it->size() > largest->size()) largest = it;

    r.group = *largest;
    r.alive = false;
    r.saving_moves = savingCandidates(board, r.group);
    r.reason = r.saving_moves.empty()
                   ? "该块没有任何气口，无法做出两只眼"
                   : "该块被判死，但存在可能的挣扎点，需进一步分析";
    return r;
}

}  // namespace vgo
