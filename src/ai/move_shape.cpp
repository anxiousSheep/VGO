#include "move_shape.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "go/handicap.h"
#include "gtp.h"

namespace vgo {

namespace {

int cheb(int dx, int dy) { return std::max(std::abs(dx), std::abs(dy)); }

// 线数：距四条边的最小距离 + 1（1 = 一线，四四 = 4，三三 = 3）。
int lineOf(const Point& p, int size) {
    const int m = std::min({p.x, p.y, size - 1 - p.x, size - 1 - p.y});
    return m + 1;
}

// 距 p 最近的异色子；无则返回 false。
bool nearestOpponent(const Board& b, Point p, Color mover, Point* q) {
    const Color opp = opponent(mover);
    int best = 1 << 30;
    bool found = false;
    for (int y = 0; y < b.size(); ++y) {
        for (int x = 0; x < b.size(); ++x) {
            if (b.at(x, y) != opp) continue;
            const int d = cheb(p.x - x, p.y - y);
            if (!found || d < best) {
                best = d;
                *q = {x, y};
                found = true;
            }
        }
    }
    return found;
}

// 标准星位点集（供 nearby）：13 路以上含 4 角 + 天元 + 4 边星，小棋盘仅 4 角 + 天元。
std::vector<Point> starPoints(int size) {
    if (size >= 13) return handicapPoints(size, 9);
    return handicapPoints(size, 5);
}

}  // namespace

ShapeFact classifyMove(const Board& b, Point p, Color mover) {
    ShapeFact f;
    Point q;
    if (!nearestOpponent(b, p, mover, &q)) {
        f.shape = MoveShape::Other;
        f.term = "普通落子";
        f.relation = gtpPoint(p, b.size()) + " 附近无异色子，属普通落子";
        return f;
    }

    const int dx = p.x - q.x;
    const int dy = p.y - q.y;
    const int mh = std::abs(dx) + std::abs(dy);
    const int ch = cheb(dx, dy);

    if (mh == 1) {
        f.shape = MoveShape::Contact;
    } else if (std::abs(dx) == 1 && std::abs(dy) == 1) {
        f.shape = MoveShape::Diagonal;
    } else if (ch == 2) {
        f.shape = MoveShape::OneGap;
    } else if (ch == 3) {
        f.shape = MoveShape::TwoGap;
    } else {
        f.shape = MoveShape::Other;
    }

    switch (f.shape) {
        case MoveShape::Contact:
            f.term = "贴";  // 扳/挡/顶的统称（正交接触）
            break;
        case MoveShape::Diagonal:
            // 三三点入侵四四点 → 点入三三（而非「尖」）。
            if (lineOf(p, b.size()) == 3 && lineOf(q, b.size()) == 4) f.term = "点";
            else f.term = "尖";
            break;
        case MoveShape::OneGap:
            // 直行隔一路 = 跳；斜行隔一路 = 飞。
            f.term = (dx == 0 || dy == 0) ? "跳" : "飞";
            break;
        case MoveShape::TwoGap:
            f.term = "大跳";
            break;
        default:
            f.term = "普通落子";
            break;
    }

    const std::string oppName = (mover == Color::Black) ? "白" : "黑";
    std::string rel = gtpPoint(p, b.size()) + " 与" + oppName + " " + gtpPoint(q, b.size());
    switch (f.shape) {
        case MoveShape::Contact: rel += " 正交相邻，属贴"; break;
        case MoveShape::Diagonal:
            rel += (f.term == "点" ? " 对角相邻，属点入三三" : " 对角相邻，属尖");
            break;
        case MoveShape::OneGap: rel += " 隔一路，属" + f.term; break;
        case MoveShape::TwoGap: rel += " 隔二路，属" + f.term; break;
        default: rel += " 距离较远，属普通落子"; break;
    }
    f.relation = rel;
    return f;
}

PointGeo describePoint(const Board& b, Point p) {
    const int size = b.size();
    PointGeo g;
    g.line = lineOf(p, size);

    // 区域：按「距边 ≤4 线」判边/角，其余为中央（线数比三分更贴合围棋盘角/边习惯）。
    const int band = 4;
    const bool top = p.y < band;
    const bool bottom = p.y >= size - band;
    const bool left = p.x < band;
    const bool right = p.x >= size - band;
    if (top && right) g.region = "右上角";
    else if (top && left) g.region = "左上角";
    else if (bottom && right) g.region = "右下角";
    else if (bottom && left) g.region = "左下角";
    else if (top) g.region = "上边";
    else if (bottom) g.region = "下边";
    else if (left) g.region = "左边";
    else if (right) g.region = "右边";
    else g.region = "中央";

    // nearby：最近星位 / 天元。
    const int mid = size / 2;
    Point best{-1, -1};
    int bestd = 1 << 30;
    for (const Point& s : starPoints(size)) {
        const int d = cheb(p.x - s.x, p.y - s.y);
        if (d < bestd) {
            bestd = d;
            best = s;
        }
    }
    if (best.x >= 0) {
        const bool tengen = (best.x == mid && best.y == mid);
        if (bestd == 0) {
            g.nearby = tengen ? "即天元" : ("即星位 " + gtpPoint(best, size));
        } else if (bestd <= 2) {
            const std::string name = tengen ? "天元" : ("星位 " + gtpPoint(best, size));
            g.nearby = "距" + name + " " + std::to_string(bestd) + " 路";
        }
    }
    return g;
}

}  // namespace vgo
