#include "llm_presets.h"

namespace vgo {

const std::vector<LlmPreset>& llmPresets() {
    static const std::vector<LlmPreset> kPresets = {
        {"ollama", "Ollama（本地）", "http://localhost:11434/v1", "", false},
        {"deepseek", "DeepSeek", "https://api.deepseek.com/v1", "deepseek-chat", true},
        {"qwen", "通义千问", "https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-plus", true},
        {"kimi", "Moonshot/Kimi", "https://api.moonshot.cn/v1", "moonshot-v1-8k", true},
        {"zhipu", "智谱", "https://open.bigmodel.cn/api/paas/v4", "glm-4-flash", true},
        {"openai", "OpenAI", "https://api.openai.com/v1", "gpt-4o-mini", true},
        {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "", true},
        {"custom", "自定义", "", "", false},
    };
    return kPresets;
}

const LlmPreset* findPreset(const std::string& id) {
    for (const auto& p : llmPresets()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

}  // namespace vgo
