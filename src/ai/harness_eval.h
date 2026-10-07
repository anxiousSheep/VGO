#pragma once

#include <string>
#include <vector>

#include "conversation.h"

namespace vgo {

// 一条 harness golden 路由样本（D5.7）：自然语言提问 + 期望的问题分类。
// 覆盖「改写说法」的提问（同一意图换多种说法），作为 D1「模型调工具」之前的
// 关键词路由兜底的离线回归基线——release gate 判据（命中率 ≥ 90%）。
struct HarnessGoldenCase {
    std::string question;
    QuestionClass expected_cls;
};

// 一次路由回归的报告（D5.7 release gate 的判据）。
struct HarnessEvalReport {
    int total = 0;
    int classify_hits = 0;
    double classify_hit_rate = 0.0;

    bool pass(double threshold = 0.9) const;
};

// 用 golden 集对关键词路由打分（纯逻辑、可复跑）。这是 D1「关键词分发降级为兜底」
// 的离线完成标准：即便模型工具环路不可用，兜底路由也不得明显回退。
HarnessEvalReport evaluateHarnessRouting(const std::vector<HarnessGoldenCase>& golden);

// 随代码内置的 harness golden 集（覆盖 6 类问题 + 各类改写说法），保证 CTest 回归
// 闸门 hermetic、可复跑。
std::vector<HarnessGoldenCase> builtinHarnessGolden();

}  // namespace vgo
