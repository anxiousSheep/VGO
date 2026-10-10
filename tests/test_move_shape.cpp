#include "test_framework.h"

#include <string>

#include "ai/gtp.h"
#include "ai/move_shape.h"

using namespace vgo;

namespace {

Point P(const std::string& gtp, int size = 19) {
    Point p;
    gtpParsePoint(gtp, size, &p);
    return p;
}

}  // namespace

// —— G1 着法几何（候选点相对最近异色子）——

TEST(move_shape_contact_is_tie) {
    Board b(19);
    b.set(5, 5, Color::Black);
    const ShapeFact f = classifyMove(b, {6, 5}, Color::White);  // 正交相邻
    CHECK(f.shape == MoveShape::Contact);
    CHECK_EQ(f.term, std::string("贴"));
}

TEST(move_shape_diagonal_is_jian) {
    Board b(19);
    b.set(5, 5, Color::Black);
    const ShapeFact f = classifyMove(b, {6, 6}, Color::White);  // 对角相邻
    CHECK(f.shape == MoveShape::Diagonal);
    CHECK_EQ(f.term, std::string("尖"));
}

TEST(move_shape_sansan_invasion_is_dian_not_hane) {
    // 现象 ① 核心：四四 Q16 的对角三三点 R17 是「点入三三」而非「扳/尖」。
    Board b(19);
    const Point anchor = P("Q16");
    b.set(anchor.x, anchor.y, Color::Black);
    const ShapeFact f = classifyMove(b, P("R17"), Color::White);
    CHECK(f.shape == MoveShape::Diagonal);
    CHECK_EQ(f.term, std::string("点"));
    CHECK(f.relation.find("点入三三") != std::string::npos);
    CHECK(f.relation.find("扳") == std::string::npos);
}

TEST(move_shape_one_gap_straight_is_jump) {
    Board b(19);
    b.set(5, 5, Color::Black);
    const ShapeFact f = classifyMove(b, {7, 5}, Color::White);  // 直行隔一路
    CHECK(f.shape == MoveShape::OneGap);
    CHECK_EQ(f.term, std::string("跳"));
}

TEST(move_shape_one_gap_knight_is_fly) {
    Board b(19);
    b.set(5, 5, Color::Black);
    const ShapeFact f = classifyMove(b, {7, 6}, Color::White);  // 小飞 (2,1)
    CHECK(f.shape == MoveShape::OneGap);
    CHECK_EQ(f.term, std::string("飞"));
}

TEST(move_shape_two_gap_is_big_jump) {
    Board b(19);
    b.set(5, 5, Color::Black);
    const ShapeFact f = classifyMove(b, {8, 5}, Color::White);  // 隔二路
    CHECK(f.shape == MoveShape::TwoGap);
    CHECK_EQ(f.term, std::string("大跳"));
}

TEST(move_shape_no_opponent_is_plain) {
    Board b(19);
    const ShapeFact f = classifyMove(b, {5, 5}, Color::White);
    CHECK(f.shape == MoveShape::Other);
    CHECK_EQ(f.term, std::string("普通落子"));
}

// —— G2 空间事实（describePoint）19 路 golden ——

TEST(describe_point_19_tengen) {
    Board b(19);
    const PointGeo g = describePoint(b, {9, 9});
    CHECK_EQ(g.line, 10);
    CHECK_EQ(g.region, std::string("中央"));
    CHECK_EQ(g.nearby, std::string("即天元"));
}

TEST(describe_point_19_star) {
    Board b(19);
    const PointGeo g = describePoint(b, P("Q16"));
    CHECK_EQ(g.line, 4);
    CHECK_EQ(g.region, std::string("右上角"));
    CHECK_EQ(g.nearby, std::string("即星位 Q16"));
}

TEST(describe_point_19_sansan) {
    Board b(19);
    const PointGeo g = describePoint(b, P("R17"));
    CHECK_EQ(g.line, 3);
    CHECK_EQ(g.region, std::string("右上角"));
    CHECK_EQ(g.nearby, std::string("距星位 Q16 1 路"));
}

TEST(describe_point_19_komoku) {
    Board b(19);
    const PointGeo g = describePoint(b, P("R16"));  // 3-4 小目
    CHECK_EQ(g.line, 3);
    CHECK_EQ(g.region, std::string("右上角"));
    CHECK_EQ(g.nearby, std::string("距星位 Q16 1 路"));
}

TEST(describe_point_19_four_edges) {
    Board b(19);
    CHECK_EQ(describePoint(b, {9, 0}).region, std::string("上边"));
    CHECK_EQ(describePoint(b, {9, 18}).region, std::string("下边"));
    CHECK_EQ(describePoint(b, {0, 9}).region, std::string("左边"));
    CHECK_EQ(describePoint(b, {18, 9}).region, std::string("右边"));
}

// —— G2 空间事实（describePoint）9 路 golden ——

TEST(describe_point_9_tengen) {
    Board b(9);
    const PointGeo g = describePoint(b, {4, 4});
    CHECK_EQ(g.line, 5);
    CHECK_EQ(g.region, std::string("中央"));
    CHECK_EQ(g.nearby, std::string("即天元"));
}

TEST(describe_point_9_corner_star) {
    Board b(9);
    const PointGeo g = describePoint(b, {2, 2});  // 9 路星位即三三
    CHECK_EQ(g.line, 3);
    CHECK_EQ(g.region, std::string("左上角"));
    CHECK_EQ(g.nearby, std::string("即星位 C7"));
}

TEST(describe_point_9_komoku) {
    Board b(9);
    const PointGeo g = describePoint(b, {3, 2});
    CHECK_EQ(g.line, 3);
    CHECK_EQ(g.region, std::string("左上角"));
    CHECK_EQ(g.nearby, std::string("距星位 C7 1 路"));
}
