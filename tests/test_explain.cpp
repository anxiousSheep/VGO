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

// --- B1a：几何着法识别 ---

TEST(explain_hane) {
    // 扳：己方子在对角，且该己方子与落点共享的某正交邻点为对方子（贴住对方拐头）。
    Board before(5);
    before.set(2, 2, Color::Black);
    before.set(3, 2, Color::White);
    Board after = before;
    after.set(3, 3, Color::Black);

    const auto facts = extractBoardFacts(before, after, Move{Point{3, 3}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::Hane));
    CHECK(!hasFact(facts, FactType::Diagonal));
    CHECK(renderExplanation(facts, Color::Black).find("扳") != std::string::npos);
}

TEST(explain_diagonal) {
    // 尖：己方子在对角，且不满足扳（共享正交邻点无对方子）。
    Board before(5);
    before.set(2, 2, Color::Black);
    Board after = before;
    after.set(3, 3, Color::Black);

    const auto facts = extractBoardFacts(before, after, Move{Point{3, 3}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::Diagonal));
    CHECK(!hasFact(facts, FactType::Hane));
    CHECK(renderExplanation(facts, Color::Black).find("尖") != std::string::npos);
}

TEST(explain_small_knight) {
    Board before(9);
    before.set(2, 2, Color::Black);
    Board after = before;
    after.set(4, 3, Color::Black);  // dx=2, dy=1

    const auto facts = extractBoardFacts(before, after, Move{Point{4, 3}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::SmallKnight));
    CHECK(renderExplanation(facts, Color::Black).find("小飞") != std::string::npos);
}

TEST(explain_large_knight) {
    Board before(9);
    before.set(2, 2, Color::Black);
    Board after = before;
    after.set(5, 4, Color::Black);  // dx=3, dy=2

    const auto facts = extractBoardFacts(before, after, Move{Point{5, 4}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::LargeKnight));
    CHECK(renderExplanation(facts, Color::Black).find("大飞") != std::string::npos);
}

TEST(explain_one_space_jump) {
    Board before(9);
    before.set(2, 2, Color::Black);
    Board after = before;
    after.set(4, 2, Color::Black);  // dx=2, dy=0

    const auto facts = extractBoardFacts(before, after, Move{Point{4, 2}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::OneSpaceJump));
    CHECK(renderExplanation(facts, Color::Black).find("跳") != std::string::npos);
}

TEST(explain_cut) {
    // 断：落点 4 个正交邻点上属于不同对方组的数量 >= 2（把对方两块棋切开）。
    Board before(5);
    before.set(2, 2, Color::White);
    before.set(4, 2, Color::White);  // two separate white groups
    Board after = before;
    after.set(3, 2, Color::Black);   // cuts between them

    const auto facts = extractBoardFacts(before, after, Move{Point{3, 2}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::Cut));
    CHECK(renderExplanation(facts, Color::Black).find("切断") != std::string::npos);
}

TEST(explain_tiger_mouth) {
    // 虎：两对角己方子 + 空口 + 落点（面向西，口在 (x-1,y)）。
    Board before(5);
    before.set(2, 2, Color::Black);
    before.set(2, 4, Color::Black);
    Board after = before;
    after.set(3, 3, Color::Black);

    const auto facts = extractBoardFacts(before, after, Move{Point{3, 3}, false}, 0, Color::Black);
    CHECK(hasFact(facts, FactType::TigerMouth));
    CHECK(renderExplanation(facts, Color::Black).find("虎") != std::string::npos);
}

TEST(explain_no_shape_when_isolated) {
    // 负例：无己方子（Chebyshev 距离 > 3）→ 无主形状，维持普通落子。
    Board before(9);
    Board after = before;
    after.set(4, 4, Color::Black);

    const auto facts = extractBoardFacts(before, after, Move{Point{4, 4}, false}, 0, Color::Black);
    CHECK(!hasFact(facts, FactType::Hane));
    CHECK(!hasFact(facts, FactType::Diagonal));
    CHECK(!hasFact(facts, FactType::SmallKnight));
    CHECK(!hasFact(facts, FactType::LargeKnight));
    CHECK(!hasFact(facts, FactType::OneSpaceJump));
    CHECK(!hasFact(facts, FactType::TigerMouth));
    CHECK(hasFact(facts, FactType::Placement));
    CHECK(renderExplanation(facts, Color::Black).find("普通落子") != std::string::npos);
}
