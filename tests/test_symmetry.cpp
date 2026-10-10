#include "test_framework.h"

#include <set>
#include <vector>

#include "ai/symmetry.h"

using namespace vgo;

namespace {
// All 8 symmetries, in a fixed order (for round-trip and invariance loops).
const std::vector<Symmetry> kAll = {
    Symmetry::Id,       Symmetry::Rot90,     Symmetry::Rot180, Symmetry::Rot270,
    Symmetry::MirrorX,  Symmetry::MirrorY,   Symmetry::MirrorDiag, Symmetry::MirrorAntiDiag,
};

std::set<std::pair<int, int>> toSet(const std::vector<Point>& pts) {
    std::set<std::pair<int, int>> s;
    for (const Point& p : pts) s.insert({p.x, p.y});
    return s;
}
}  // namespace

TEST(symmetry_apply_maps_ur_to_each_corner) {
    const int n = 9;
    const Point ur{8, 0};
    CHECK(applySymmetry(Symmetry::Id, ur, n) == (Point{8, 0}));
    CHECK(applySymmetry(Symmetry::MirrorX, ur, n) == (Point{0, 0}));
    CHECK(applySymmetry(Symmetry::Rot180, ur, n) == (Point{0, 8}));
    CHECK(applySymmetry(Symmetry::MirrorY, ur, n) == (Point{8, 8}));
}

TEST(symmetry_corner_mapping) {
    CHECK(cornerSymmetry(Corner::UR) == Symmetry::Id);
    CHECK(cornerSymmetry(Corner::UL) == Symmetry::MirrorX);
    CHECK(cornerSymmetry(Corner::BL) == Symmetry::Rot180);
    CHECK(cornerSymmetry(Corner::BR) == Symmetry::MirrorY);
}

TEST(symmetry_inverse_roundtrip) {
    const int n = 9;
    for (Symmetry s : kAll) {
        const Point p{3, 5};
        const Point q = applySymmetry(s, p, n);
        const Point r = applySymmetry(inverseSymmetry(s), q, n);
        CHECK(r == p);
    }
}

TEST(symmetry_canonicalize_invariant_under_d4) {
    // 刀五的角部图案（2×3 缺角），非方形包围盒，用于验证对称对非方形的处理。
    const std::vector<Point> base = {{0, 0}, {1, 0}, {2, 0}, {1, 1}, {2, 1}};
    const std::string base_key = shapeKey(canonicalize(base).points);

    // 对每种对称 + 一个任意平移，得到同形图案；canonicalize 后键必须一致。
    for (Symmetry s : kAll) {
        std::vector<Point> t;
        for (const Point& p : base) {
            Point q = applySymmetry(s, p, 4);
            q.x += 7;  // 任意平移
            q.y += 3;
            t.push_back(q);
        }
        const std::string k = shapeKey(canonicalize(t).points);
        CHECK_EQ(k, base_key);
    }
}

TEST(symmetry_canonicalize_translates_to_origin) {
    const std::vector<Point> pts = {{5, 6}, {6, 6}, {5, 7}};
    const CanonicalShape cs = canonicalize(pts);
    int minx = 100, miny = 100;
    for (const Point& p : cs.points) {
        minx = std::min(minx, p.x);
        miny = std::min(miny, p.y);
    }
    CHECK_EQ(minx, 0);
    CHECK_EQ(miny, 0);
}

TEST(symmetry_canonical_to_board_roundtrip) {
    // 输入点集在真实棋盘上（带平移），canonicalToBoard 应把它反投影回原集合。
    const std::vector<Point> pts = {{5, 3}, {6, 3}, {5, 4}, {6, 4}};
    const CanonicalShape cs = canonicalize(pts);

    std::vector<Point> recovered;
    for (const Point& c : cs.points) recovered.push_back(canonicalToBoard(cs, c));
    CHECK(toSet(recovered) == toSet(pts));
}

TEST(symmetry_shape_key_format) {
    const std::vector<Point> pts = {{1, 2}, {0, 3}};
    CHECK_EQ(shapeKey(pts), std::string("1,2;0,3"));
    const std::vector<Point> empty;
    CHECK_EQ(shapeKey(empty), std::string(""));
}
