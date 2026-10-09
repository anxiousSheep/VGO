#include "test_framework.h"

#include "ai/phase.h"

using namespace vgo;

// --- B2：局面阶段判定（纯逻辑） ---

TEST(phase_classify_hand_thresholds) {
    // 9 路：Fuseki < 18，Endgame > 40（size*size/2）。
    CHECK(classifyPhase(0, 9, OwnershipMap{}) == GamePhase::Fuseki);
    CHECK(classifyPhase(17, 9, OwnershipMap{}) == GamePhase::Fuseki);
    CHECK(classifyPhase(18, 9, OwnershipMap{}) == GamePhase::MiddleGame);
    CHECK(classifyPhase(40, 9, OwnershipMap{}) == GamePhase::MiddleGame);
    CHECK(classifyPhase(41, 9, OwnershipMap{}) == GamePhase::Endgame);
    // 19 路：Fuseki < 38，Endgame > 180。
    CHECK(classifyPhase(0, 19, OwnershipMap{}) == GamePhase::Fuseki);
    CHECK(classifyPhase(30, 19, OwnershipMap{}) == GamePhase::Fuseki);
    CHECK(classifyPhase(38, 19, OwnershipMap{}) == GamePhase::MiddleGame);
    CHECK(classifyPhase(180, 19, OwnershipMap{}) == GamePhase::MiddleGame);
    CHECK(classifyPhase(300, 19, OwnershipMap{}) == GamePhase::Endgame);
}

TEST(phase_classify_ownership_solidified) {
    // 主信号：领地大面积定型（|ownership| > 0.8 的点占比 > 0.55）优先于手数。
    OwnershipMap om;
    om.size = 9;
    om.value.assign(81, 0.0);
    for (int i = 0; i < 60; ++i) om.value[i] = 1.0;  // 60/81 ≈ 0.74 > 0.55
    CHECK(classifyPhase(10, 9, om) == GamePhase::Endgame);

    // 未固化：回退到手数阈值。
    OwnershipMap loose;
    loose.size = 9;
    loose.value.assign(81, 0.5);  // |0.5| < 0.8
    CHECK(classifyPhase(10, 9, loose) == GamePhase::Fuseki);
}

TEST(phase_classify_empty_ownership_fallback) {
    // ownership 为空（fallback 引擎）时只用 move_number 阈值。
    CHECK(classifyPhase(10, 9, OwnershipMap{}) == GamePhase::Fuseki);
    CHECK(classifyPhase(100, 9, OwnershipMap{}) == GamePhase::Endgame);
}

TEST(phase_label) {
    CHECK_EQ(std::string(phaseLabel(GamePhase::Fuseki)), std::string("布局"));
    CHECK_EQ(std::string(phaseLabel(GamePhase::MiddleGame)), std::string("中盘"));
    CHECK_EQ(std::string(phaseLabel(GamePhase::Endgame)), std::string("官子"));
}

// --- B2：手顺串（纯逻辑） ---

TEST(phase_move_sequence_format) {
    std::vector<std::pair<Color, Move>> moves;
    moves.push_back({Color::Black, Move{Point{2, 2}, false}});
    moves.push_back({Color::White, Move{Point{3, 3}, false}});
    const std::string s = buildMoveSequence(moves, 9, 10);
    CHECK_EQ(s, std::string("黑 C7、白 D6"));
}

TEST(phase_move_sequence_pass_rendered) {
    std::vector<std::pair<Color, Move>> moves;
    moves.push_back({Color::Black, Move{Point{-1, -1}, true}});
    CHECK_EQ(buildMoveSequence(moves, 9, 10), std::string("黑 虚着"));
}

TEST(phase_move_sequence_truncates) {
    std::vector<std::pair<Color, Move>> moves;
    for (int i = 0; i < 12; ++i)
        moves.push_back({(i % 2 == 0) ? Color::Black : Color::White, Move{Point{i, i}, false}});
    const std::string s = buildMoveSequence(moves, 19, 10);
    // 只取最近 10 手：第 1 手（i=0 → A19）、第 2 手（i=1 → B18）被裁掉。
    CHECK(s.find("A19") == std::string::npos);
    CHECK(s.find("B18") == std::string::npos);
}

TEST(phase_move_sequence_empty) {
    CHECK(buildMoveSequence({}, 9, 10).empty());
}
