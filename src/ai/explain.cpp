#include "explain.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#include "gtp.h"

namespace vgo {

namespace {

constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Number of distinct groups of `me` among the four neighbors of (x, y).
int distinctOwnGroups(const Board& b, int x, int y, Color me) {
    const int n = b.size();
    std::vector<bool> visited(static_cast<std::size_t>(n) * n, false);
    int groups = 0;
    for (const auto [dx, dy] : kDirs) {
        const int nx = x + dx;
        const int ny = y + dy;
        if (!b.in_bounds(nx, ny) || b.at(nx, ny) != me) continue;
        if (visited[b.index(nx, ny)]) continue;
        ++groups;
        for (const Point& p : b.group(nx, ny)) visited[b.index(p.x, p.y)] = true;
    }
    return groups;
}

// Mirror of distinctOwnGroups, counting distinct `me` groups (B1a 断). The two
// share the same logic; the separate name keeps call sites self-documenting.
int distinctOppGroups(const Board& b, int x, int y, Color me) {
    return distinctOwnGroups(b, x, y, me);
}

// Chebyshev distance between two offsets.
int chebDist(int dx, int dy) {
    return std::max(std::abs(dx), std::abs(dy));
}

// B1a 扳：own stone at (±1,±1), and one of the two orthogonal neighbors shared
// with the落点 (x,y) — i.e. (x+dx,y) or (x,y+dy) — is an opponent stone.
bool isHane(const Board& after, int x, int y, int dx, int dy, Color mover, Color opp) {
    if (after.at(x + dx, y + dy) != mover) return false;
    return after.at(x + dx, y) == opp || after.at(x, y + dy) == opp;
}

// Priority order for tie-breaking at equal Chebyshev distance: 扳 > 尖 > 小飞 >
// 大飞 > 跳 (B1a §1.2). Lower value = higher priority.
int shapePriority(FactType t) {
    switch (t) {
        case FactType::Hane:         return 0;
        case FactType::Diagonal:     return 1;
        case FactType::SmallKnight:  return 2;
        case FactType::LargeKnight:  return 3;
        case FactType::OneSpaceJump: return 4;
        default:                     return 99;
    }
}

// B1a 主形状：取 Chebyshev 距离最近的己方子确定形状；距离并列按优先级取先者；
// 距离 > 3 无己方子则无主形状（返回 nullopt）。
std::optional<FactType> mainShape(const Board& after, int x, int y, Color mover, Color opp) {
    FactType best = FactType::Placement;
    int bestDist = 4;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int d = chebDist(dx, dy);
            if (d > 3 || after.at(x + dx, y + dy) != mover) continue;

            std::optional<FactType> shape;
            if (d == 1 && std::abs(dx) == 1 && std::abs(dy) == 1) {
                shape = isHane(after, x, y, dx, dy, mover, opp) ? FactType::Hane
                                                                : FactType::Diagonal;
            } else if ((std::abs(dx) == 1 && std::abs(dy) == 2) ||
                       (std::abs(dx) == 2 && std::abs(dy) == 1)) {
                shape = FactType::SmallKnight;
            } else if ((std::abs(dx) == 2 && std::abs(dy) == 3) ||
                       (std::abs(dx) == 3 && std::abs(dy) == 2)) {
                shape = FactType::LargeKnight;
            } else if ((std::abs(dx) == 2 && dy == 0) || (dx == 0 && std::abs(dy) == 2)) {
                shape = FactType::OneSpaceJump;
            }
            if (!shape) continue;

            if (d < bestDist) {
                bestDist = d;
                best = *shape;
            } else if (d == bestDist && shapePriority(*shape) < shapePriority(best)) {
                best = *shape;
            }
        }
    }
    if (bestDist > 3) return std::nullopt;
    return best;
}

// B1a 虎口：存在一个方向 d，使落点 + 两个与落点、彼此皆对角相邻的己方子，且二者
// 之间的正交点（虎口）为空。四个方向分别判：西/东（口在 (x±1,y)）、北/南（口在
// (x,y±1)）。
bool isTigerMouth(const Board& after, int x, int y, Color mover) {
    // 每方向：(口偏移, 两个对角己方子偏移)。
    const std::array<std::pair<int, int>, 4> mouths = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    const std::array<std::array<std::pair<int, int>, 2>, 4> stones = {{
        {{{-1, -1}, {-1, 1}}},  // 西
        {{{1, -1}, {1, 1}}},    // 东
        {{{-1, -1}, {1, -1}}},  // 北
        {{{-1, 1}, {1, 1}}},    // 南
    }};
    for (int d = 0; d < 4; ++d) {
        const auto [mx, my] = mouths[d];
        if (!after.in_bounds(x + mx, y + my) || after.at(x + mx, y + my) != Color::Empty)
            continue;
        const auto [s1, s2] = stones[d];
        if (after.at(x + s1.first, y + s1.second) == mover &&
            after.at(x + s2.first, y + s2.second) == mover)
            return true;
    }
    return false;
}

}  // namespace

std::vector<StructuralFact> extractBoardFacts(const Board& before, const Board& after,
                                              const Move& move, int captured,
                                              Color mover) {
    std::vector<StructuralFact> facts;
    if (move.is_pass) return facts;

    const Color opp = opponent(mover);
    const int x = move.p.x;
    const int y = move.p.y;

    // Captures: stones that disappeared from the board.
    if (captured > 0) {
        StructuralFact f;
        f.type = FactType::Captures;
        for (int py = 0; py < after.size(); ++py) {
            for (int px = 0; px < after.size(); ++px) {
                if (before.at(px, py) == opp && after.at(px, py) == Color::Empty) {
                    f.stones.push_back({px, py});
                }
            }
        }
        facts.push_back(std::move(f));
    }

    // Atari on opponent groups adjacent to the move.
    {
        std::vector<Point> atari;
        for (const auto [dx, dy] : kDirs) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (after.in_bounds(nx, ny) && after.at(nx, ny) == opp &&
                after.liberties(nx, ny).size() == 1) {
                for (const Point& p : after.group(nx, ny)) atari.push_back(p);
            }
        }
        if (!atari.empty()) {
            StructuralFact f;
            f.type = FactType::PutsInAtari;
            f.stones = std::move(atari);
            facts.push_back(std::move(f));
        }
    }

    // Own group state after the move.
    const int ownLiberties = static_cast<int>(after.liberties(x, y).size());
    if (ownLiberties == 1) {
        StructuralFact f;
        f.type = FactType::SelfAtari;
        f.liberties = 1;
        facts.push_back(std::move(f));
    }

    // B1a 断：落点 4 个正交邻点上属于不同对方组的数量 >= 2（把对方两块棋切开）。
    if (distinctOppGroups(after, x, y, opp) >= 2) {
        StructuralFact f;
        f.type = FactType::Cut;
        facts.push_back(std::move(f));
    }

    // B1a 虎口：两对角己方子 + 空口 + 落点。
    if (isTigerMouth(after, x, y, mover)) {
        StructuralFact f;
        f.type = FactType::TigerMouth;
        facts.push_back(std::move(f));
    }

    // B1a 主形状（扳/尖/小飞/大飞/跳）：按最近己方子的几何相对位置判定。
    if (auto shape = mainShape(after, x, y, mover, opp)) {
        StructuralFact f;
        f.type = *shape;
        facts.push_back(std::move(f));
    }

    // Connection / extension / placement.
    const int beforeGroups = distinctOwnGroups(before, x, y, mover);
    if (beforeGroups >= 2) {
        StructuralFact f;
        f.type = FactType::Connects;
        f.liberties = ownLiberties;
        facts.push_back(std::move(f));
    } else if (beforeGroups == 1) {
        StructuralFact f;
        f.type = FactType::Extends;
        f.liberties = ownLiberties;
        facts.push_back(std::move(f));
    } else {
        StructuralFact f;
        f.type = FactType::Placement;
        f.liberties = ownLiberties;
        facts.push_back(std::move(f));
    }

    return facts;
}

std::string renderExplanation(const std::vector<StructuralFact>& facts, Color mover) {
    if (facts.empty()) return {};
    const char* side = (mover == Color::Black) ? "黑" : "白";
    std::vector<std::string> parts;
    int captured = 0;
    bool atari = false;
    bool connect = false;
    bool extend = false;
    bool self = false;
    bool placement = false;
    bool cut = false;
    bool tiger = false;
    std::string shape;  // 主形状短语（扳/尖/小飞/大飞/跳，至多一个）
    for (const auto& f : facts) {
        switch (f.type) {
            case FactType::Captures: captured += static_cast<int>(f.stones.size()); break;
            case FactType::PutsInAtari: atari = true; break;
            case FactType::Connects: connect = true; break;
            case FactType::Extends: extend = true; break;
            case FactType::SelfAtari: self = true; break;
            case FactType::Placement: placement = true; break;
            case FactType::Cut: cut = true; break;
            case FactType::Hane: shape = "扳（贴住对方拐头）"; break;
            case FactType::Diagonal: shape = "尖（小尖）"; break;
            case FactType::SmallKnight: shape = "小飞"; break;
            case FactType::LargeKnight: shape = "大飞"; break;
            case FactType::OneSpaceJump: shape = "跳（一间跳）"; break;
            case FactType::TigerMouth: tiger = true; break;
        }
    }
    if (captured > 0)
        parts.push_back(std::string(side) + "提掉对方 " + std::to_string(captured) + " 子");
    if (atari) parts.push_back("把对方的一块棋打成只剩一口气（打吃）");
    if (connect) parts.push_back("把己方分开的两块棋连成了一块");
    if (extend) parts.push_back("延展并加强了己方的一块棋");
    if (cut) parts.push_back("切断对方两块棋");
    if (!shape.empty()) parts.push_back(shape);
    if (tiger) parts.push_back("虎（虎口）");
    if (self) parts.push_back("（提示）这手棋自己的气较紧，只剩一口气");
    if (parts.empty() && placement) parts.push_back("这是一手普通落子");

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += "，";
        out += parts[i];
    }
    return out;
}

std::string boardToAscii(const Board& board) {
    const int n = board.size();
    std::ostringstream os;
    for (int y = 0; y < n; ++y) {
        os << (n - y) << ((n - y) < 10 ? "  " : " ");
        for (int x = 0; x < n; ++x) os << stone_char(board.at(x, y)) << ' ';
        os << '\n';
    }
    os << "   ";
    for (int x = 0; x < n; ++x) {
        char c = static_cast<char>('A' + x);
        if (c >= 'I') ++c;  // skip 'I'
        os << c << ' ';
    }
    os << '\n';
    return os.str();
}

std::string renderMoveEvaluation(const MoveEvaluation& e, int size) {
    if (!e.legal) return "这个点不能落子（已占 / 自杀 / 打劫）。";
    if (!e.valid) return "未接入 KataGo，我无法量化评估这一步棋的得失。";
    const char* side = (e.mover == Color::Black)   ? "黑方"
                       : (e.mover == Color::White) ? "白方"
                                                   : "落子方";
    const double dw = (e.winrate_after - e.winrate_before) * 100.0;
    const double dl = e.lead_after - e.lead_before;
    std::ostringstream os;
    os << "若在 " << gtpPoint(e.p, size) << " 落子，" << side << "胜率从 " << std::fixed
       << std::setprecision(1) << e.winrate_before * 100.0 << "% 变为 "
       << e.winrate_after * 100.0 << "%";
    if (dw >= 0.05) os << "（升 " << std::abs(dw) << " 个百分点）";
    else if (dw <= -0.05) os << "（降 " << std::abs(dw) << " 个百分点）";
    else os << "（基本不变）";
    os << "；领先从 " << std::setprecision(1) << e.lead_before << " 目变为 " << e.lead_after
       << " 目";
    if (dl >= 0.05) os << "（约多得 " << std::abs(dl) << " 目）";
    else if (dl <= -0.05) os << "（约亏 " << std::abs(dl) << " 目）";
    else os << "（目数基本不变）";
    os << "。";
    return os.str();
}

std::string renderLifeDeath(const LifeDeathResult& r, int size) {
    if (r.alive || r.group.empty()) return "当前没有能被确认判死的棋。";
    std::ostringstream os;
    os << "有一块棋被判死：";
    for (std::size_t i = 0; i < r.group.size() && i < 6; ++i)
        os << gtpPoint(r.group[i], size) << ' ';
    if (r.saving_moves.empty()) {
        os << "。它没有任何气口，试遍候选点也无法做出两只眼，所以做不活。";
    } else {
        os << "。可能的挣扎点是 ";
        for (std::size_t i = 0; i < r.saving_moves.size() && i < 4; ++i)
            os << gtpPoint(r.saving_moves[i], size) << ' ';
        os << "，但能否救活需要进一步分析确认。";
    }
    return os.str();
}

std::string renderDeadGroups(const std::vector<std::vector<Point>>& groups, int) {
    if (groups.empty()) return {};
    return "已确认的死棋块：" + std::to_string(groups.size()) + " 块。";
}

std::string moveHint(const Point& p, int size) {
    if (p.x < 0 || p.y < 0 || size <= 1) return "整盘都可以考虑";
    const double fx = static_cast<double>(p.x) / (size - 1);
    const double fy = static_cast<double>(p.y) / (size - 1);
    std::string v;
    if (fy < 0.33) v = "上";
    else if (fy > 0.67) v = "下";
    std::string h;
    if (fx < 0.33) h = "左";
    else if (fx > 0.67) h = "右";
    if (v.empty() && h.empty()) return "棋盘中央附近";
    return "往" + h + v + "方向看";
}

}  // namespace vgo
