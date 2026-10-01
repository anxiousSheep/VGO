#include "test_framework.h"

#include "ai/llm_presets.h"

using namespace vgo;

TEST(presets_ollama_first_no_key) {
    CHECK(!llmPresets().empty());
    CHECK(llmPresets().front().id == "ollama");
    CHECK(!llmPresets().front().needsKey);
}

TEST(presets_find_known) {
    const LlmPreset* p = findPreset("deepseek");
    CHECK(p != nullptr);
    CHECK(p->baseUrl == "https://api.deepseek.com/v1");
    CHECK(p->needsKey);
    CHECK(findPreset("does-not-exist") == nullptr);
}
