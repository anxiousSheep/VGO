#pragma once

#include <string>

namespace vgo {

// The quality tier of the explanation currently being served (D5.1 "明示降级").
// The UI shows a persistent badge so the user always knows which tier they are
// getting — instead of silently falling back to the template.
enum class ExplanationTier {
    FullKataGoLlm,  // KataGo 分析 + 强 LLM 讲解
    LocalLlm,       // 本地模型（无 KataGo，或仅本地 7B）
    Template,       // 规则引擎 + 模板解说（离线兜底）
};

// Resolve the tier from what is actually available.
ExplanationTier resolveTier(bool hasKataGo, bool hasLlm);

// Short stable label for the status-bar badge, e.g. "KataGo+LLM".
const char* tierLabel(ExplanationTier t);

// Longer human-readable description, e.g. "KataGo 分析 + 语言模型讲解".
std::string tierDescription(ExplanationTier t);

}  // namespace vgo
