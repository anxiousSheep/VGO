#include "degradation.h"

namespace vgo {

ExplanationTier resolveTier(bool hasKataGo, bool hasLlm) {
    if (hasKataGo && hasLlm) return ExplanationTier::FullKataGoLlm;
    if (hasLlm) return ExplanationTier::LocalLlm;
    return ExplanationTier::Template;
}

const char* tierLabel(ExplanationTier t) {
    switch (t) {
        case ExplanationTier::FullKataGoLlm: return "KataGo+LLM";
        case ExplanationTier::LocalLlm: return "本地模型";
        case ExplanationTier::Template: default: return "模板解说";
    }
}

std::string tierDescription(ExplanationTier t) {
    switch (t) {
        case ExplanationTier::FullKataGoLlm:
            return "KataGo 分析 + 语言模型讲解";
        case ExplanationTier::LocalLlm:
            return "语言模型讲解（未接 KataGo）";
        case ExplanationTier::Template:
        default:
            return "规则引擎 + 模板解说（离线）";
    }
}

}  // namespace vgo
