#include "test_framework.h"

#include <cmath>

#include "ai/evaluate.h"
#include "ai/explain.h"

using namespace vgo;

namespace {
Analysis validAnalysis(double winrate, double lead) {
    Analysis a;
    a.valid = true;
    a.winrate = winrate;
    a.score_lead = lead;
    return a;
}

struct ScriptedAnalyzeEngine : EngineInterface {
    std::vector<Analysis> analyses;
    int calls = 0;
    void requestMove(const Board&, Color, const StrengthParams&, Callback) override {}
    void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
        Analysis a;
        if (calls < static_cast<int>(analyses.size())) a = analyses[static_cast<std::size_t>(calls)];
        ++calls;
        cb(a);
    }
};
}  // namespace

TEST(find_point_in_question) {
    const PointMatch m = findPointInQuestion("下 G3 怎么样", 9);
    CHECK(m.found);
    CHECK_EQ(m.p.x, 6);  // 'G' -> x=6
    CHECK_EQ(m.p.y, 6);  // row 3 -> y = 9-3 = 6

    const PointMatch lower = findPointInQuestion("这里下 c5 行吗", 9);
    CHECK(lower.found);
    CHECK_EQ(lower.p.x, 2);
    CHECK_EQ(lower.p.y, 4);

    CHECK(!findPointInQuestion("这步怎么样", 9).found);
}

TEST(compute_move_evaluation_perspective) {
    MoveEvaluation in;
    in.p = {3, 3};
    in.legal = true;
    // before: Black to move, Black winrate 55%, lead +2
    // after:  White to move, White winrate 40%, White lead -1
    const MoveEvaluation e =
        computeMoveEvaluation(in, validAnalysis(0.55, 2.0), Color::Black,
                              validAnalysis(0.40, -1.0), Color::White, Color::Black);
    CHECK(e.valid);
    CHECK(std::abs(e.winrate_before - 0.55) < 1e-9);
    CHECK(std::abs(e.winrate_after - 0.60) < 1e-9);  // 1 - 0.40
    CHECK(std::abs(e.lead_before - 2.0) < 1e-9);
    CHECK(std::abs(e.lead_after - 1.0) < 1e-9);  // -(-1)
}

TEST(compute_move_evaluation_invalid_when_missing) {
    MoveEvaluation in;
    in.p = {3, 3};
    const MoveEvaluation e =
        computeMoveEvaluation(in, Analysis{}, Color::Black, validAnalysis(0.5, 0.0),
                              Color::White, Color::Black);
    CHECK(!e.valid);
}

TEST(is_blunder_thresholds) {
    MoveEvaluation e;
    e.legal = true;
    e.valid = true;
    e.winrate_before = 0.60;
    e.winrate_after = 0.40;  // -0.20 winrate, -4 lead -> blunder
    e.lead_before = 2.0;
    e.lead_after = -2.0;
    CHECK(isBlunder(e));
    CHECK(!blunderWarning(e).empty());

    e.winrate_after = 0.55;
    e.lead_after = 1.0;
    CHECK(!isBlunder(e));
    CHECK(blunderWarning(e).empty());
}

TEST(move_evaluator_async_whatif) {
    ScriptedAnalyzeEngine eng;
    eng.analyses = {validAnalysis(0.55, 2.0), validAnalysis(0.40, -1.0)};
    MoveEvaluator ev(&eng);
    Game g(9);

    MoveEvaluation got;
    bool done = false;
    ev.evaluate(g, {3, 3}, [&](const MoveEvaluation& e) {
        got = e;
        done = true;
    });
    CHECK(done);
    CHECK(got.legal);
    CHECK(got.valid);
    CHECK(std::abs(got.winrate_before - 0.55) < 1e-9);
    CHECK(std::abs(got.winrate_after - 0.60) < 1e-9);
    CHECK_EQ(eng.calls, 2);
}

TEST(move_evaluator_illegal_point) {
    ScriptedAnalyzeEngine eng;
    MoveEvaluator ev(&eng);
    Game g(9);
    g.play(3, 3);  // (3,3) now occupied, it is White's turn

    MoveEvaluation got;
    ev.evaluate(g, {3, 3}, [&](const MoveEvaluation& e) { got = e; });
    CHECK(!got.legal);
    CHECK_EQ(eng.calls, 0);
}

TEST(move_evaluator_no_engine_valid_false) {
    MoveEvaluator ev(nullptr);
    Game g(9);
    MoveEvaluation got;
    ev.evaluate(g, {3, 3}, [&](const MoveEvaluation& e) { got = e; });
    CHECK(got.legal);
    CHECK(!got.valid);
}

TEST(render_move_evaluation_and_hint) {
    MoveEvaluation e;
    e.p = {6, 6};
    e.legal = true;
    e.valid = true;
    e.winrate_before = 0.55;
    e.winrate_after = 0.40;
    e.lead_before = 2.0;
    e.lead_after = -1.0;
    const std::string s = renderMoveEvaluation(e, 9);
    CHECK(!s.empty());
    CHECK(s.find("55") != std::string::npos);
    CHECK(s.find("40") != std::string::npos);

    CHECK_EQ(moveHint({0, 0}, 9), std::string("往左上方向看"));
    CHECK_EQ(moveHint({8, 8}, 9), std::string("往右下方向看"));
}
