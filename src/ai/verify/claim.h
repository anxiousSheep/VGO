#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../go/go_types.h"

namespace vgo {

// A machine-checkable assertion extracted from a model reply (D2 "Verify-then-
// Speak"). The model is asked to also emit a `<claims>` block; when it does not,
// the extractor falls back to scanning the prose for coordinates / numbers.
enum class ClaimType {
    ScoreLead,  // "黑领先 5.5 目"  (value = lead in points, from color's view)
    Winrate,    // "黑胜率 62%"      (value in [0,1], from color's view)
    GroupLife,  // "C3 这块棋死了"   (point anchor + alive verdict)
};

struct Claim {
    ClaimType type = ClaimType::ScoreLead;
    bool valid = false;             // parsed successfully
    double value = 0.0;             // lead (points) or winrate ([0,1])
    Color color = Color::Empty;     // the side that leads / is alive
    Point point{-1, -1};            // GroupLife anchor point
    bool alive = true;              // GroupLife verdict
    std::string source;             // provenance set by the verifier
};

// Three-tier disposition of a claim against ground truth (D2).
enum class ClaimDisposition {
    Pass,          // 与真值一致：正常渲染
    Corrected,     // 数值在容差外但可修正：改写为引擎真值并标注
    Unverifiable,  // 无对应真值（如模型自造坐标）：灰显 + unsure
};

struct ClaimVerdict {
    Claim claim;
    ClaimDisposition disposition = ClaimDisposition::Pass;
    std::optional<double> truth;  // engine truth (for Corrected)
    std::string note;             // short human-readable explanation
};

}  // namespace vgo
