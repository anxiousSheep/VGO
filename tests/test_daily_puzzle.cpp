#include "test_framework.h"

#include <string>

#include "ai/learner/daily_puzzle.h"
#include "ai/learner/learner_model.h"

using namespace vgo;

TEST(judge_passes_and_scores_mid) {
    // 恶手前领先 10，恶手后领先 4（掉 6 目）；新落点领先 7。
    // improvement = 3，max = 6 → ratio 0.5 → 60 + 20 = 80。
    const DailyVerdict v = judgeDailyMove(7.0, 4.0, 10.0);
    CHECK(v.passed);
    CHECK_EQ(v.improvement, 3.0);
    CHECK_EQ(v.score, 80);
}

TEST(judge_best_move_full_score) {
    // 新落点恢复到恶手前领先 → improvement = max → 100。
    const DailyVerdict v = judgeDailyMove(10.0, 4.0, 10.0);
    CHECK(v.passed);
    CHECK_EQ(v.score, 100);
}

TEST(judge_equal_to_blunder_fails) {
    // 与恶手持平（improvement = 0）不算过。
    const DailyVerdict v = judgeDailyMove(4.0, 4.0, 10.0);
    CHECK(!v.passed);
    CHECK_EQ(v.score, 0);
}

TEST(judge_worse_fails) {
    const DailyVerdict v = judgeDailyMove(2.0, 4.0, 10.0);
    CHECK(!v.passed);
    CHECK(v.improvement < 0.0);
}

TEST(judge_score_clamped_to_100) {
    // 找到比引擎当时最佳还好的点 → improvement > max → 钳制 100。
    const DailyVerdict v = judgeDailyMove(12.0, 4.0, 10.0);
    CHECK(v.passed);
    CHECK_EQ(v.score, 100);
}

TEST(judge_score_floor_60) {
    // 极小提升 → 四舍五入到 60（最低过关分）。
    const DailyVerdict v = judgeDailyMove(4.01, 4.0, 10.0);
    CHECK(v.passed);
    CHECK_EQ(v.score, 60);
}

TEST(board_from_snapshot_roundtrip) {
    Board b(3);
    b.set(0, 0, Color::Black);
    b.set(1, 1, Color::White);
    const std::string snap = b.snapshot();
    Board out;
    CHECK(boardFromSnapshot(snap, 3, &out));
    CHECK(out.at(0, 0) == Color::Black);
    CHECK(out.at(1, 1) == Color::White);
    CHECK(out.at(2, 2) == Color::Empty);
    CHECK_EQ(out.size(), 3);
}

TEST(board_from_snapshot_rejects_bad_size) {
    Board out;
    CHECK(!boardFromSnapshot("XXXX", 3, &out));  // 长度 4 != 9
}

TEST(snapshot_hash_stable) {
    CHECK_EQ(snapshotHash("X.."), snapshotHash("X.."));
    CHECK(snapshotHash("X..") != snapshotHash("O.."));
}

TEST(review_item_with_board_snapshot) {
    // 带棋盘快照时：id 由快照哈希稳定生成（跨局去重），且局面字段齐全。
    const std::string snap = std::string(9, '.');  // 3x3 空盘
    const ReviewItem it = reviewItemFromBlunder(8, Point{1, 1}, "掉 3 目",
                                                WeakArea::Ko, 100, 3, snap,
                                                5.0, 2.0, Color::Black);
    CHECK_EQ(it.id, std::string("blunder-") + snapshotHash(snap));
    CHECK_EQ(it.size, 3);
    CHECK(it.board_snapshot == snap);
    CHECK(it.to_move == Color::Black);
    CHECK(it.blunder_move.x == 1);
    CHECK(it.blunder_move.y == 1);
    CHECK_EQ(it.lead_before, 5.0);
    CHECK_EQ(it.lead_blunder, 2.0);
}
