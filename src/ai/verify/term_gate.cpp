#include "term_gate.h"

namespace vgo {

namespace {

// 着法术语集合（单字为主，避免把「长」「顶」等常见字误伤得过宽）。
const char* const kTerms[] = {
    "扳", "断", "尖", "飞", "跳", "虎", "点", "挂", "夹",
    "贴", "长", "爬", "挡", "顶", "觑", "拆",
};

}  // namespace

bool termGateViolation(const std::string& text, const std::string& expectedTerm) {
    if (expectedTerm.empty()) return false;
    const bool has_expected = text.find(expectedTerm) != std::string::npos;
    // 正文若用了期望术语 → 通过；否则，用了任一其他着法术语 → 判为术语改写。
    if (has_expected) return false;
    for (const char* t : kTerms) {
        if (t == expectedTerm) continue;
        if (text.find(t) != std::string::npos) return true;
    }
    return false;
}

}  // namespace vgo
