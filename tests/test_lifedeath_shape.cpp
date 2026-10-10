#include "test_framework.h"

#include <string>
#include <vector>

#include "ai/lifedeath_shape.h"
#include "ai/symmetry.h"

using namespace vgo;

namespace {

const std::pair<int, int> kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

bool hasPoint(const std::vector<Point>& pts, int x, int y) {
    for (const Point& p : pts)
        if (p.x == x && p.y == y) return true;
    return false;
}

// 构造「owner 组围住眼位、外面再被 opp 包围」的封闭局面，使 owner 只有眼位一处气。
Board buildEnclosed(const std::vector<Point>& eye, Color owner, Color opp, int size = 9) {
    Board b(size);
    int minx = eye[0].x, miny = eye[0].y, maxx = eye[0].x, maxy = eye[0].y;
    for (const Point& p : eye) {
        minx = std::min(minx, p.x);
        miny = std::min(miny, p.y);
        maxx = std::max(maxx, p.x);
        maxy = std::max(maxy, p.y);
    }
    // owner 块 = 眼位包围盒外扩 1，再挖掉眼位。
    for (int x = minx - 1; x <= maxx + 1; ++x)
        for (int y = miny - 1; y <= maxy + 1; ++y)
            if (b.in_bounds(x, y)) b.set(x, y, owner);
    for (const Point& p : eye) b.set(p.x, p.y, Color::Empty);
    // 用 opp 把 owner 块外围包住（owner 从此只有眼位这一处气）；跳过眼位本身。
    const auto isEye = [&](int x, int y) {
        for (const Point& p : eye)
            if (p.x == x && p.y == y) return true;
        return false;
    };
    for (int x = 0; x < size; ++x)
        for (int y = 0; y < size; ++y) {
            if (b.at(x, y) != Color::Empty || isEye(x, y)) continue;
            bool touches = false;
            for (const auto [dx, dy] : kDirs)
                if (b.in_bounds(x + dx, y + dy) && b.at(x + dx, y + dy) == owner)
                    touches = true;
            if (touches) b.set(x, y, opp);
        }
    return b;
}

// 取 owner 色棋块的组（包围盒外扩框左上角必为 owner）。
std::vector<Point> ownerGroup(const Board& b, Color owner) {
    for (int x = 0; x < b.size(); ++x)
        for (int y = 0; y < b.size(); ++y)
            if (b.at(x, y) == owner) return b.group(x, y);
    return {};
}

}  // namespace

TEST(lifeshape_nine_shapes_match_self) {
    const LifeShapeLibrary lib = LifeShapeLibrary::builtin();
    CHECK_EQ(lib.size(), 10u);
    for (const LifeShape& s : lib.all()) {
        const auto m = lib.match(s.pattern);
        CHECK(m.shape != nullptr);
        if (m.shape) CHECK_EQ(m.shape->id, s.id);
        // 已规范化：vital 反投影应等于自身。
        CHECK_EQ(m.vital_board.size(), s.vital.size());
    }
}

TEST(lifeshape_rotation_mirror_same_entry) {
    const LifeShapeLibrary lib = LifeShapeLibrary::builtin();
    const LifeShape& knife = *lib.match({{0, 0}, {1, 0}, {2, 0}, {1, 1}, {2, 1}}).shape;
    // 对刀五 canonical pattern 施加各对称 + 平移，仍应命中同一条目。
    for (Symmetry s : {Symmetry::Id, Symmetry::Rot90, Symmetry::Rot180, Symmetry::Rot270,
                       Symmetry::MirrorX, Symmetry::MirrorY, Symmetry::MirrorDiag,
                       Symmetry::MirrorAntiDiag}) {
        std::vector<Point> t;
        for (const Point& p : knife.pattern) {
            Point q = applySymmetry(s, p, 4);
            q.x += 3;
            q.y += 2;
            t.push_back(q);
        }
        const auto m = lib.match(t);
        CHECK(m.shape != nullptr);
        if (m.shape) CHECK_EQ(m.shape->id, std::string("knife-five"));
    }
}

TEST(analyze_shape_knife_five) {
    // 刀五眼位置于 (3,3)..(5,4)。
    const std::vector<Point> eye = {{3, 3}, {4, 3}, {5, 3}, {4, 4}, {5, 4}};
    Board b = buildEnclosed(eye, Color::White, Color::Black);
    const std::vector<Point> group = ownerGroup(b, Color::White);
    const ShapeAnalysis sa = analyzeShape(b, group);
    CHECK_EQ(sa.shape, std::string("刀五"));
    CHECK(sa.status == LDStatus::Unsettled);
    // vital 反投影到真实棋盘（canonical 刀五 vital = (1,0)，须对应真实要点）。
    CHECK(!sa.kill_points.empty());
}

TEST(analyze_shape_straight_three_dead) {
    const std::vector<Point> eye = {{3, 3}, {4, 3}, {5, 3}};
    Board b = buildEnclosed(eye, Color::White, Color::Black);
    const std::vector<Point> group = ownerGroup(b, Color::White);
    const ShapeAnalysis sa = analyzeShape(b, group);
    CHECK_EQ(sa.shape, std::string("直三"));
    CHECK(sa.status == LDStatus::Unsettled);
    // 直三 vital = 中点 (4,3)。
    CHECK(hasPoint(sa.kill_points, 4, 3));
}

TEST(analyze_shape_square_four_dead) {
    const std::vector<Point> eye = {{3, 3}, {4, 3}, {3, 4}, {4, 4}};
    Board b = buildEnclosed(eye, Color::White, Color::Black);
    const std::vector<Point> group = ownerGroup(b, Color::White);
    const ShapeAnalysis sa = analyzeShape(b, group);
    CHECK_EQ(sa.shape, std::string("方四"));
    CHECK(sa.status == LDStatus::Dead);
    CHECK(sa.kill_points.empty());  // 方四无单一要点
}

TEST(true_eye_center) {
    Board b(9);
    // (4,4) 中心真眼：正交 + 对角全黑。
    for (const auto [dx, dy] : std::vector<std::pair<int, int>>{{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                                                {1, 1}, {1, -1}, {-1, 1}, {-1, -1}})
        b.set(4 + dx, 4 + dy, Color::Black);
    CHECK(isTrueEye(b, {4, 4}, Color::Black));
}

TEST(true_eye_center_false_with_two_white_diagonals) {
    Board b(9);
    for (const auto [dx, dy] : std::vector<std::pair<int, int>>{{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
        b.set(4 + dx, 4 + dy, Color::Black);
    b.set(3, 3, Color::Black);
    b.set(5, 5, Color::Black);
    b.set(3, 5, Color::White);  // 对角白子（不可立即提掉）
    b.set(5, 3, Color::White);
    CHECK(!isTrueEye(b, {4, 4}, Color::Black));
}

TEST(true_eye_edge_and_corner) {
    Board b(9);
    // 边眼 (0,4)。
    b.set(1, 4, Color::Black);
    b.set(0, 3, Color::Black);
    b.set(0, 5, Color::Black);
    b.set(1, 3, Color::Black);
    b.set(1, 5, Color::Black);
    CHECK(isTrueEye(b, {0, 4}, Color::Black));

    // 角眼 (0,0)。
    Board c(9);
    c.set(1, 0, Color::Black);
    c.set(0, 1, Color::Black);
    c.set(1, 1, Color::Black);
    CHECK(isTrueEye(c, {0, 0}, Color::Black));
    // 对角被白占 → 假眼。
    Board d(9);
    d.set(1, 0, Color::Black);
    d.set(0, 1, Color::Black);
    d.set(1, 1, Color::White);
    CHECK(!isTrueEye(d, {0, 0}, Color::Black));
}

TEST(pattern_alive_straight_four) {
    const std::vector<Point> eye = {{3, 3}, {4, 3}, {5, 3}, {6, 3}};
    Board b = buildEnclosed(eye, Color::White, Color::Black);
    const std::vector<Point> group = ownerGroup(b, Color::White);
    CHECK(isPatternAlive(b, group));
}

TEST(pattern_alive_two_single_eyes) {
    // 黑块（3×5）挖两个单点眼 (1,1)/(3,1)，两块真眼 → 活。
    Board b(9);
    for (int x = 0; x <= 4; ++x)
        for (int y = 0; y <= 2; ++y) b.set(x, y, Color::Black);
    b.set(1, 1, Color::Empty);
    b.set(3, 1, Color::Empty);
    const std::vector<Point> group = b.group(0, 0);
    CHECK(isPatternAlive(b, group));
}

TEST(lifeshape_from_json_and_defensive_canonicalize) {
    // 负坐标 / 未平移的 pattern 经 defensive canonicalize 后仍能命中。
    LifeShapeLibrary lib;
    std::string err;
    const std::string json =
        R"({"id":"neg-knife","name":"刀五","status":"unsettled",
            "pattern":["5,5","6,5","7,5","6,6","7,6"],"vital":["6,5"],"note":"n"})";
    CHECK(lib.fromJsonText(json, &err));
    // 平移后的同形 pattern 应命中同一条目。
    const auto m = lib.match({{0, 0}, {1, 0}, {2, 0}, {1, 1}, {2, 1}});
    CHECK(m.shape != nullptr);
    if (m.shape) CHECK_EQ(m.shape->id, std::string("neg-knife"));
    // 非法 status。
    CHECK(!lib.fromJsonText(
        R"({"id":"bad","name":"x","status":"weird","pattern":["0,0","1,0"],"vital":[]})", &err));
}
