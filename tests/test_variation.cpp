#include "test_framework.h"

#include "ai/variation.h"

using namespace vgo;

TEST(variation_commit_and_board) {
    VariationController c;
    Board start(9);
    c.start(start, Color::Black);
    CHECK(c.active());

    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    Board b = c.board();
    CHECK(b.at(3, 3) == Color::Black);
    CHECK(b.at(0, 0) == Color::Empty);
    CHECK(c.toMove() == Color::White);
    CHECK(c.lastStep() != nullptr);
    CHECK_EQ(c.lastStep()->move.p.x, 3);
    CHECK_EQ(c.lastStep()->move.p.y, 3);
}

TEST(variation_back_removes_last) {
    VariationController c;
    c.start(Board(9), Color::Black);
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    c.commitStep(Move{{6, 6}, false}, Color::White, true);
    CHECK_EQ(c.variation().steps.size(), 2u);

    CHECK(c.back());
    CHECK_EQ(c.variation().steps.size(), 1u);
    CHECK(c.board().at(3, 3) == Color::Black);
    CHECK(c.board().at(6, 6) == Color::Empty);
    // 回退后可见的最后一手就是第 1 步（修复 off-by-one）。
    CHECK(c.lastStep() != nullptr);
    CHECK_EQ(c.lastStep()->move.p.x, 3);
    CHECK_EQ(c.lastStep()->move.p.y, 3);

    CHECK(c.back());
    CHECK(c.variation().steps.empty());
    CHECK(!c.back());
}

TEST(variation_to_move_flips) {
    VariationController c;
    c.start(Board(9), Color::Black);
    CHECK(c.toMove() == Color::Black);
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    CHECK(c.toMove() == Color::White);
}

TEST(variation_exit_deactivates) {
    VariationController c;
    c.start(Board(9), Color::Black);
    c.exit();
    CHECK(!c.active());
    // 退出后 commit/back 都是 no-op。
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    CHECK(c.variation().steps.empty());
    CHECK(!c.back());
}
