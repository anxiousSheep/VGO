#include "test_framework.h"

#include "ai/explain.h"
#include "go/game.h"

using namespace vgo;

namespace {

bool hasFact(const std::vector<StructuralFact>& facts, FactType t) {
    for (const auto& f : facts) {
        if (f.type == t) return true;
    }
    return false;
}

}  // namespace

TEST(explain_capture) {
    Board before(5);
    before.set(1, 1, Color::White);
    before.set(0, 1, Color::Black);
    before.set(2, 1, Color::Black);
    before.set(1, 0, Color::Black);
    Board after = before;
    after.set(1, 2, Color::Black);
    after.set(1, 1, Color::Empty);

    const auto facts = extractBoardFacts(before, after, Move{Point{1, 2}, false}, 1, Color::Black);
    CHECK(hasFact(facts, FactType::Captures));
    const std::string s = renderExplanation(facts, Color::Black);
    CHECK(!s.empty());
    CHECK(s.find("提掉") != std::string::npos);
}

TEST(explain_connect) {
    Board before(5);
    before.set(0, 0, Color::Black);
    before.set(0, 2, Color::Black);  // two separate groups
    Board after = before;
    after.set(0, 1, Color::Black);   // bridges them

    const auto facts = extractBoardFacts(before, after, Move{Point{0, 1}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::Connects));
    CHECK(renderExplanation(facts, Color::Black).find("连成") != std::string::npos);
}

TEST(explain_atari) {
    Board before(5);
    before.set(1, 1, Color::White);
    before.set(0, 1, Color::Black);
    before.set(2, 1, Color::Black);  // White(1,1) now has liberties (1,0),(1,2)
    Board after = before;
    after.set(1, 0, Color::Black);   // takes one more liberty -> atari

    const auto facts = extractBoardFacts(before, after, Move{Point{1, 0}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::PutsInAtari));
    CHECK(renderExplanation(facts, Color::Black).find("打吃") != std::string::npos);
}

TEST(explain_extend) {
    Board before(5);
    before.set(2, 2, Color::White);
    Board after = before;
    after.set(2, 3, Color::White);

    const auto facts = extractBoardFacts(before, after, Move{Point{2, 3}, false}, 0, Color::White);
    CHECK(hasFact(facts, FactType::Extends));
}

TEST(explain_placement) {
    Board before(5);
    Board after = before;
    after.set(2, 2, Color::Black);  // isolated stone

    const auto facts = extractBoardFacts(before, after, Move{Point{2, 2}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::Placement));
}

TEST(explain_pass_gives_no_facts) {
    Board before(5);
    const auto facts = extractBoardFacts(before, before, Move{Point{-1, -1}, true}, 0, Color::Black);
    CHECK_EQ(facts.size(), std::size_t(0));
}

TEST(explain_ascii_board) {
    Board b(5);
    b.set(2, 2, Color::Black);
    const std::string s = boardToAscii(b);
    CHECK(!s.empty());
    CHECK(s.find('X') != std::string::npos);
    CHECK(s.find("A") != std::string::npos);
}
