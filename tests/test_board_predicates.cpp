#include "test_framework.h"

#include <string>
#include <vector>

#include "ai/board_predicates.h"
#include "ai/gtp.h"
#include "ai/joseki.h"

using namespace vgo;

namespace {

Point P(const std::string& gtp) {
    Point p;
    gtpParsePoint(gtp, 19, &p);
    return p;
}

const JosekiEntry* sansan() {
    static const JosekiLibrary lib = JosekiLibrary::builtin();
    return lib.find("hoshi-sansan-invasion");
}

Board emptyBoard() { return Board(19); }

Board boardWithBlackAnchor() {
    Board b(19);
    const Point a = P("Q16");  // 星位 4-4（黑）
    b.set(a.x, a.y, Color::Black);
    return b;
}

}  // namespace

TEST(predicates_satisfied_with_anchor_and_clear_path) {
    const Board b = boardWithBlackAnchor();
    const PrecondReport r = checkPreconditions(b, *sansan(), Corner::UR);
    CHECK(r.satisfied);
    CHECK(r.missing.empty());
    CHECK(r.occupied_path.empty());
}

TEST(predicates_missing_anchor) {
    const Board b = emptyBoard();
    const PrecondReport r = checkPreconditions(b, *sansan(), Corner::UR);
    CHECK(!r.satisfied);
    CHECK(!r.missing.empty());
}

TEST(predicates_occupied_path) {
    Board b = boardWithBlackAnchor();
    const Point p = P("R17");  // 3-3 入侵点已被占
    b.set(p.x, p.y, Color::White);
    const PrecondReport r = checkPreconditions(b, *sansan(), Corner::UR);
    CHECK(!r.satisfied);
    CHECK(!r.occupied_path.empty());
}

TEST(predicates_size_mismatch) {
    const Board b(9);
    const PrecondReport r = checkPreconditions(b, *sansan(), Corner::UR);
    CHECK(!r.satisfied);
    CHECK(!r.missing.empty());
}

TEST(predicates_classify_context_flanks) {
    Board b = boardWithBlackAnchor();
    const Point s = P("S14");  // 右翼接应子（白=入侵方）
    b.set(s.x, s.y, Color::White);
    CHECK(josekiInvader(*sansan()) == Color::White);
    const CornerContext ctx = classifyContext(b, *sansan(), Corner::UR, Color::White);
    CHECK(ctx.right_friendly >= 1);
    CHECK_EQ(ctx.left_friendly, 0);
    CHECK(ctx.path_clear);
}

TEST(predicates_select_default_and_reverse_on_right_flank) {
    Board b = boardWithBlackAnchor();
    const Point s = P("S14");
    b.set(s.x, s.y, Color::White);  // 右翼白接应
    const std::vector<int> sel = selectVariants(b, *sansan(), Corner::UR, Color::White);
    bool has_main = false, has_reverse = false;
    for (int i : sel) {
        if (sansan()->variants[static_cast<std::size_t>(i)].id == "main") has_main = true;
        if (sansan()->variants[static_cast<std::size_t>(i)].id == "reverse-block")
            has_reverse = true;
    }
    CHECK(has_main);       // 默认变体恒在（可摆线基线）
    CHECK(has_reverse);    // 右翼接应 → 反向挡建议
}

TEST(predicates_select_only_default_when_no_flank) {
    const Board b = boardWithBlackAnchor();
    const std::vector<int> sel = selectVariants(b, *sansan(), Corner::UR, Color::White);
    CHECK_EQ(sel.size(), 1u);
    CHECK_EQ(sansan()->variants[static_cast<std::size_t>(sel[0])].id, std::string("main"));
}

TEST(predicates_direction_mirrors_on_right) {
    CornerContext ctx;
    ctx.right_friendly = 2;
    ctx.left_friendly = 0;
    CHECK(directionMirrors(ctx, "block_toward_friendly_side"));
    CHECK(!directionMirrors(ctx, ""));
}

TEST(predicates_evaluate_when_four_kinds) {
    CornerContext ctx;
    CHECK(evaluateWhen("default", ctx));
    CHECK(!evaluateWhen("ladder_favorable", ctx));  // B1b 落地前恒不命中
    ctx.left_friendly = 2;
    CHECK(evaluateWhen("zone_friendly_majority:left_flank", ctx));
    ctx.right_enemy = 3;
    CHECK(evaluateWhen("zone_enemy_majority:right_flank", ctx));
    CHECK(!evaluateWhen("nonsense", ctx));
}

TEST(predicates_direction_symmetry_corners) {
    CHECK(directionSymmetry(Corner::UR) == Symmetry::MirrorAntiDiag);
    CHECK(directionSymmetry(Corner::BL) == Symmetry::MirrorAntiDiag);
    CHECK(directionSymmetry(Corner::UL) == Symmetry::MirrorDiag);
    CHECK(directionSymmetry(Corner::BR) == Symmetry::MirrorDiag);
}

TEST(predicates_project_variant_mirror_flips_block) {
    const JosekiEntry* e = sansan();
    const JosekiVariant* main = nullptr;
    for (const JosekiVariant& v : e->variants)
        if (v.id == "main") main = &v;
    CHECK(main != nullptr);
    if (!main) return;

    CornerContext no_mirror;
    CornerContext mirror;
    mirror.right_friendly = 1;  // 触发反向挡镜像
    const std::vector<JosekiMove> a = projectVariant(*main, 19, Corner::UR, no_mirror);
    const std::vector<JosekiMove> b = projectVariant(*main, 19, Corner::UR, mirror);
    CHECK_EQ(a.size(), b.size());
    CHECK(a[0].p == b[0].p);     // 3-3 入侵点固定
    CHECK(!(a[1].p == b[1].p));  // 挡点被镜像到另一侧
}
