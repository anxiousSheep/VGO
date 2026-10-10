#include "test_framework.h"

#include <utility>
#include <vector>

#include "ai/tsumego.h"

using namespace vgo;

namespace {

Move m(int x, int y) { return Move{Point{x, y}, false}; }

// 构造一道多分支题：黑(解方)先手。
//   root A(黑,good) → B(白) → { C(黑,good,无子=Solved), D(黑,bad) }
TsumegoProblem makeProblem() {
    TsumegoProblem p;
    p.id = "test";
    p.size = 9;
    p.to_move = Color::Black;

    TsumegoNode a;
    a.move = m(1, 1);
    a.by = Color::Black;
    a.verdict = "good";

    TsumegoNode b;
    b.move = m(2, 1);
    b.by = Color::White;

    TsumegoNode c;
    c.move = m(1, 2);
    c.by = Color::Black;
    c.verdict = "good";  // 无子 → Solved

    TsumegoNode d;
    d.move = m(3, 1);
    d.by = Color::Black;
    d.verdict = "bad";
    d.comment = "方向错了";

    b.children.push_back(std::move(c));
    b.children.push_back(std::move(d));
    a.children.push_back(std::move(b));
    p.solution_tree = std::move(a);
    return p;
}

}  // namespace

TEST(tsumego_first_move_correct) {
    const TsumegoProblem p = makeProblem();
    const TsumegoJudgement j = judgeTsumegoMove(p, {}, m(1, 1));
    CHECK(j.judge == TsumegoJudge::Correct);
    CHECK_EQ(j.response.size(), 1u);
    if (!j.response.empty()) CHECK(j.response[0].p == (Point{2, 1}));
}

TEST(tsumego_first_move_off_tree) {
    const TsumegoProblem p = makeProblem();
    const TsumegoJudgement j = judgeTsumegoMove(p, {}, m(4, 4));
    CHECK(j.judge == TsumegoJudge::OffTree);
}

TEST(tsumego_second_move_good_solved) {
    const TsumegoProblem p = makeProblem();
    const std::vector<std::pair<Color, Move>> path = {{Color::Black, m(1, 1)},
                                                      {Color::White, m(2, 1)}};
    const TsumegoJudgement j = judgeTsumegoMove(p, path, m(1, 2));
    CHECK(j.judge == TsumegoJudge::Solved);
}

TEST(tsumego_second_move_bad) {
    const TsumegoProblem p = makeProblem();
    const std::vector<std::pair<Color, Move>> path = {{Color::Black, m(1, 1)},
                                                      {Color::White, m(2, 1)}};
    const TsumegoJudgement j = judgeTsumegoMove(p, path, m(3, 1));
    CHECK(j.judge == TsumegoJudge::Wrong);
    CHECK(j.comment.find("方向") != std::string::npos);
}

TEST(tsumego_second_move_off_tree) {
    const TsumegoProblem p = makeProblem();
    const std::vector<std::pair<Color, Move>> path = {{Color::Black, m(1, 1)},
                                                      {Color::White, m(2, 1)}};
    const TsumegoJudgement j = judgeTsumegoMove(p, path, m(9, 9));
    CHECK(j.judge == TsumegoJudge::OffTree);
}

TEST(tsumego_inconsistent_path_off_tree) {
    const TsumegoProblem p = makeProblem();
    // path[0] 与根不符 → OffTree。
    const std::vector<std::pair<Color, Move>> path = {{Color::Black, m(8, 8)}};
    const TsumegoJudgement j = judgeTsumegoMove(p, path, m(1, 2));
    CHECK(j.judge == TsumegoJudge::OffTree);
}
