#include "test_framework.h"

#include <cmath>

#include "ai/engine_kata/kata_engine.h"

using namespace vgo;

TEST(parse_kata_analysis_rootinfo_and_ownership) {
    // Space-separated text as returned by `kata-search_analyze` (tokens may be
    // on one line or split across lines).
    const char* text =
        "info move D4 visits 100 winrate 0.61 scoreLead 3.0 order 0 pv D4 E5 "
        "rootInfo visits 200 winrate 0.62 scoreLead 3.5 "
        "ownership 0.1 -0.2 0.3";
    Analysis a;
    CHECK(parseKataAnalysis(QByteArray(text), 9, Color::Black, &a));
    CHECK(a.valid);
    CHECK(std::abs(a.winrate - 0.62) < 1e-9);       // from rootInfo
    CHECK(std::abs(a.score_lead - 3.5) < 1e-9);

    CHECK_EQ(a.candidates.size(), 1u);
    CHECK_EQ(a.candidates[0].p.x, 3);   // "D" -> x=3
    CHECK_EQ(a.candidates[0].p.y, 5);   // row 4 -> y = 9-4 = 5
    CHECK_EQ(a.candidates[0].pv.size(), 2u);  // D4, E5
    CHECK_EQ(a.best.p.x, 3);

    CHECK_EQ(a.ownership.size, 9);
    CHECK_EQ(a.ownership.value.size(), 81u);
    // 黑方视角：符号不变。
    CHECK(std::abs(a.ownership.at(0, 0) - 0.1) < 1e-9);
    CHECK(std::abs(a.ownership.at(1, 0) + 0.2) < 1e-9);
    CHECK(std::abs(a.ownership.at(2, 0) - 0.3) < 1e-9);
}

TEST(parse_kata_analysis_ownership_white_perspective_negated) {
    // KataGo 的 ownership 以行棋方为视角；白方行棋时需取反转成黑方视角。
    const char* text = "rootInfo visits 100 winrate 0.5 scoreLead 0.0 "
                       "ownership 0.4 -0.6";
    Analysis a;
    CHECK(parseKataAnalysis(QByteArray(text), 9, Color::White, &a));
    CHECK(a.valid);
    CHECK(std::abs(a.ownership.at(0, 0) + 0.4) < 1e-9);  // +0.4 -> -0.4
    CHECK(std::abs(a.ownership.at(1, 0) - 0.6) < 1e-9);  // -0.6 -> +0.6
}

TEST(parse_kata_analysis_multiple_moves_and_symmetry) {
    const char* text =
        "info move E5 visits 400 winrate 0.5 scoreLead 0.0 order 0 pv E5 G5 "
        "info move F5 visits 35 winrate 0.4 scoreLead -1.0 order 1 pv F5 "
        "info move D5 visits 35 isSymmetryOf F5 order 2 pv D5 "
        "rootInfo visits 500 winrate 0.5 scoreLead 0.0";
    Analysis a;
    CHECK(parseKataAnalysis(QByteArray(text), 9, Color::Black, &a));
    CHECK(a.valid);
    // Symmetry duplicates are dropped; two real candidates remain.
    CHECK_EQ(a.candidates.size(), 2u);
    CHECK_EQ(a.candidates[0].p.x, 4);  // E5 -> x=4
    CHECK_EQ(a.candidates[1].p.x, 5);  // F5 -> x=5
}

TEST(parse_kata_analysis_root_only) {
    const char* text = "rootInfo visits 500 winrate 0.5 scoreLead 0.0";
    Analysis a;
    CHECK(parseKataAnalysis(QByteArray(text), 9, Color::Black, &a));
    CHECK(a.valid);
    CHECK(a.ownership.empty());
    CHECK(a.candidates.empty());
}

TEST(parse_kata_analysis_empty_is_invalid) {
    Analysis a;
    CHECK(!parseKataAnalysis(QByteArray(""), 9, Color::Black, &a));
    CHECK(!a.valid);
    CHECK(!parseKataAnalysis(QByteArray("= \n"), 9, Color::Black, &a));
}

TEST(parse_kata_analysis_pass_move) {
    const char* text =
        "info move pass visits 10 order 0 rootInfo visits 10 winrate 0.5 scoreLead 0.0";
    Analysis a;
    CHECK(parseKataAnalysis(QByteArray(text), 9, Color::Black, &a));
    CHECK_EQ(a.candidates.size(), 1u);
    CHECK(a.candidates[0].is_pass);
}
