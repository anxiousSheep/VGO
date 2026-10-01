#pragma once

#include <string>
#include <vector>

namespace vgo {

// A preset for an OpenAI-compatible chat-completions endpoint. Pure data, no
// Qt, so it can be unit-tested headlessly.
struct LlmPreset {
    std::string id;            // stable key, stored in config
    std::string name;          // display name
    std::string baseUrl;       // chat-completions base (no trailing /v1/chat/completions)
    std::string defaultModel;  // suggested model
    bool needsKey = true;      // requires an API key
};

// All built-in presets, local-first (Ollama) then cloud providers.
const std::vector<LlmPreset>& llmPresets();

// Returns nullptr if `id` is unknown. Never fails for the "custom" preset.
const LlmPreset* findPreset(const std::string& id);

}  // namespace vgo
