#include "test_framework.h"

#include "ai/variation.h"

using namespace vgo;

TEST(variation_commit_and_board) {
    VariationController c;
    Board start(9);
    c.start(start, Color::Black);
    CHECK(c.state() == VariationState::WaitingForMove);

    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    CHECK(c.state() == VariationState::Explaining);
    Board b = c.board();
    CHECK(b.at(3, 3) == Color::Black);
    CHECK(b.at(0, 0) == Color::Empty);
}

TEST(variation_confirm_and_back) {
    VariationController c;
    c.start(Board(9), Color::Black);
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    c.appendExplanation("先占角");
    c.finishExplanation();
    CHECK(c.state() == VariationState::WaitingForConfirm);
    CHECK_EQ(c.variation().steps.front().explanation, std::string("先占角"));

    CHECK(c.confirmNext());
    CHECK(c.state() == VariationState::WaitingForMove);

    CHECK(c.back());
    CHECK_EQ(c.variation().cursor, 0);
    CHECK(c.board().at(3, 3) == Color::Empty);
    CHECK(!c.back());  // already at start
}

TEST(variation_branch_truncates_after_back) {
    VariationController c;
    c.start(Board(9), Color::Black);
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    c.finishExplanation();
    c.confirmNext();
    c.commitStep(Move{{6, 6}, false}, Color::White, true);
    c.finishExplanation();
    CHECK_EQ(c.variation().steps.size(), 2u);

    c.back();  // hide last step, cursor = 1
    c.commitStep(Move{{0, 0}, false}, Color::White, true);
    // the second step was replaced, not appended
    CHECK_EQ(c.variation().steps.size(), 2u);
    CHECK_EQ(c.variation().steps[1].move.p.x, 0);
    CHECK_EQ(c.variation().steps[1].move.p.y, 0);
}

TEST(variation_replay_and_exit) {
    VariationController c;
    c.start(Board(9), Color::Black);
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    c.replay();
    CHECK_EQ(c.variation().cursor, 0);
    c.exit();
    CHECK(c.state() == VariationState::Finished);
}

TEST(variation_to_move_flips) {
    VariationController c;
    c.start(Board(9), Color::Black);
    CHECK(c.toMove() == Color::Black);
    c.commitStep(Move{{3, 3}, false}, Color::Black, true);
    CHECK(c.toMove() == Color::White);
}
