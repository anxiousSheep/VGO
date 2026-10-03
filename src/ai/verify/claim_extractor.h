#pragma once

#include <string>
#include <vector>

#include "claim.h"

namespace vgo {

// Extracts claims from a model reply. Prefers the structured `<claims>…</claims>`
// JSON block the prompt asks for; when absent or malformed, falls back to a
// best-effort scan of the prose for coordinates / winrate / score-lead numbers.
// Pure and unit-testable (D2.1).
class ClaimExtractor {
public:
    std::vector<Claim> extract(const std::string& text, int size) const;

    // The structured path only (used for testing the JSON parsing in isolation).
    std::vector<Claim> fromClaimsBlock(const std::string& text, int size) const;
    // The prose-fallback path only.
    std::vector<Claim> fromProse(const std::string& text, int size) const;
};

}  // namespace vgo
