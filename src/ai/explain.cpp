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
    for (const auto& f : facts) {
        switch (f.type) {
            case FactType::Captures: captured += static_cast<int>(f.stones.size()); break;
            case FactType::PutsInAtari: atari = true; break;
            case FactType::Connects: connect = true; break;
            case FactType::Extends: extend = true; break;
            case FactType::SelfAtari: self = true; break;
            case FactType::Placement: placement = true; break;
        }
    }
    if (captured > 0)
        parts.push_back(std::string(side) + "提掉对方 " + std::to_string(captured) + " 子");
    if (atari) parts.push_back("把对方的一块棋打成只剩一口气（打吃）");
    if (connect) parts.push_back("把己方分开的两块棋连成了一块");
    if (extend) parts.push_back("延展并加强了己方的一块棋");
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
