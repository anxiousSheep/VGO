#include "symmetry.h"

#include <algorithm>
#include <array>

namespace vgo {

namespace {
constexpr std::array<Symmetry, kSymmetryCount> kAll = {
    Symmetry::Id,          Symmetry::Rot90,     Symmetry::Rot180,  Symmetry::Rot270,
    Symmetry::MirrorX,     Symmetry::MirrorY,   Symmetry::MirrorDiag, Symmetry::MirrorAntiDiag,
};
}  // namespace

Point applySymmetry(Symmetry s, Point p, int size) {
    const int n = size;
    const int x = p.x;
    const int y = p.y;
    switch (s) {
        case Symmetry::Id:            return {x, y};
        case Symmetry::Rot90:         return {n - 1 - y, x};        // 顺时针 90°
        case Symmetry::Rot180:        return {n - 1 - x, n - 1 - y};
        case Symmetry::Rot270:        return {y, n - 1 - x};
        case Symmetry::MirrorX:       return {n - 1 - x, y};        // 左右翻转
        case Symmetry::MirrorY:       return {x, n - 1 - y};        // 上下翻转
        case Symmetry::MirrorDiag:    return {y, x};                // 主对角线（↘）
        case Symmetry::MirrorAntiDiag:return {n - 1 - y, n - 1 - x};// 副对角线（↙）
    }
    return p;
}

Symmetry inverseSymmetry(Symmetry s) {
    switch (s) {
        case Symmetry::Id:             return Symmetry::Id;
        case Symmetry::Rot90:          return Symmetry::Rot270;
        case Symmetry::Rot180:         return Symmetry::Rot180;
        case Symmetry::Rot270:         return Symmetry::Rot90;
        case Symmetry::MirrorX:        return Symmetry::MirrorX;
        case Symmetry::MirrorY:        return Symmetry::MirrorY;
        case Symmetry::MirrorDiag:     return Symmetry::MirrorDiag;
        case Symmetry::MirrorAntiDiag: return Symmetry::MirrorAntiDiag;
    }
    return s;
}

Symmetry cornerSymmetry(Corner target) {
    switch (target) {
        case Corner::UR: return Symmetry::Id;
        case Corner::UL: return Symmetry::MirrorX;
        case Corner::BL: return Symmetry::Rot180;
        case Corner::BR: return Symmetry::MirrorY;
    }
    return Symmetry::Id;
}

CanonicalShape canonicalize(const std::vector<Point>& pts) {
    CanonicalShape cs;
    if (pts.empty()) return cs;

    int minx = pts[0].x, miny = pts[0].y, maxx = pts[0].x, maxy = pts[0].y;
    for (const Point& p : pts) {
        minx = std::min(minx, p.x);
        miny = std::min(miny, p.y);
        maxx = std::max(maxx, p.x);
        maxy = std::max(maxy, p.y);
    }
    cs.dx = minx;
    cs.dy = miny;

    // 平移到原点后的基准点集。
    std::vector<Point> base;
    base.reserve(pts.size());
    for (const Point& p : pts) base.push_back({p.x - minx, p.y - miny});

    // 对称所用的方形尺寸：包围盒最长边（对称需在方形区域上进行）。
    cs.n = std::max(maxx - minx + 1, maxy - miny + 1);

    // 8 对称中取字典序最小者。
    std::vector<Point> best;
    bool have_best = false;
    for (Symmetry s : kAll) {
        std::vector<Point> t;
        t.reserve(base.size());
        int tminx = cs.n, tminy = cs.n;
        for (const Point& p : base) {
            Point q = applySymmetry(s, p, cs.n);
            tminx = std::min(tminx, q.x);
            tminy = std::min(tminy, q.y);
            t.push_back(q);
        }
        for (Point& q : t) {
            q.x -= tminx;
            q.y -= tminy;
        }
        std::sort(t.begin(), t.end(), [](const Point& a, const Point& b) {
            return a.x != b.x ? a.x < b.x : a.y < b.y;
        });
        if (!have_best || shapeKey(t) < shapeKey(best)) {
            best = std::move(t);
            have_best = true;
            cs.used = s;
            cs.post_dx = tminx;
            cs.post_dy = tminy;
        }
    }
    cs.points = std::move(best);
    return cs;
}

std::string shapeKey(const std::vector<Point>& canonical_pts) {
    std::string s;
    for (std::size_t i = 0; i < canonical_pts.size(); ++i) {
        if (i) s += ';';
        s += std::to_string(canonical_pts[i].x) + "," + std::to_string(canonical_pts[i].y);
    }
    return s;
}

Point canonicalToBoard(const CanonicalShape& cs, Point canonical) {
    // 正向：board → 平移(-dx,-dy) → 对称(used, n) → 平移(-post_dx,-post_dy) = canonical。
    // 逆向：canonical → 平移(+post) → 对称逆(used, n) → 平移(+dx,+dy)。
    Point q{canonical.x + cs.post_dx, canonical.y + cs.post_dy};
    Point base = applySymmetry(inverseSymmetry(cs.used), q, cs.n);
    return {base.x + cs.dx, base.y + cs.dy};
}

Point boardToCanonical(const CanonicalShape& cs, Point board) {
    Point base{board.x - cs.dx, board.y - cs.dy};
    Point q = applySymmetry(cs.used, base, cs.n);
    return {q.x - cs.post_dx, q.y - cs.post_dy};
}

}  // namespace vgo
