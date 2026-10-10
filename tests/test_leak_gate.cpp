#include "test_framework.h"

#include <string>

#include "ai/verify/leak_gate.h"

using namespace vgo;

// G4-3：泄露闸门（确定性正则）。内部字段名（satisfied/occupied_path/w_joseki/w_best/
// favorable/strategy_only/verdict）一经出现即判违规；合法中文围棋讲解一律不误伤。
TEST(leak_gate_detects_each_token) {
    const char* tokens[] = {"satisfied", "occupied_path", "w_joseki", "w_best",
                            "favorable", "strategy_only", "verdict"};
    for (const char* t : tokens) {
        CHECK(leakGateViolation(std::string("这个定式的 ") + t + " 是内部数据"));
    }
    // 子串即可命中（"verdicts" 也应拦截）。
    CHECK(leakGateViolation("engine_verdicts 全被朗读了"));
}

TEST(leak_gate_passes_clean_chinese) {
    const char* clean[] = {
        "点入三三，夺角后对方挡，白爬求活，此局面两分。",
        "黑方领先 5 目，胜率约 60%。",
        "小飞挂目前不适用：第一步 O17 已被占。",
        "建议在右上角挂角，先试探对方应手。",
        "这步棋是先手，能活角，还能威胁对方外势。",
        "黑挡向夹击子一侧，使外势与夹子连片。",
    };
    for (const char* c : clean) CHECK(!leakGateViolation(c));
    CHECK(!leakGateViolation(""));
}
