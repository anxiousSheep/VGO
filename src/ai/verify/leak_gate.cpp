#include "leak_gate.h"

namespace vgo {

bool leakGateViolation(const std::string& text) {
    // 与诊断文档 G4-3 对齐的 token 集合：joseki_lookup 判定书 / JosekiVerdict 仲裁的
    // 字段名。任一命中即视为泄露。
    static const char* const kTokens[] = {
        "satisfied", "occupied_path", "w_joseki", "w_best",
        "favorable", "strategy_only", "verdict",
    };
    for (const char* t : kTokens) {
        if (text.find(t) != std::string::npos) return true;
    }
    return false;
}

}  // namespace vgo
