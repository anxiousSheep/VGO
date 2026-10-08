#pragma once

#include <cstdint>
#include <string>

namespace vgo {

// A single "bad sample" captured when the D2 verification gate intercepts a
// claim that fails against ground truth (D2.5). These accumulate into a local
// bad-sample set that feeds D4's evaluation loop (skill improvement / golden
// set expansion). Pure data, no Qt — persistence lives in the Qt layer
// (src/ui/model/BadSampleStore).
struct BadSample {
    std::int64_t ts_ms = 0;      // wall-clock timestamp (ms since epoch)
    std::string position_hash;   // deterministic hash of the position
    std::string question;        // the user's question
    std::string reply_text;      // the model's raw reply (contains the bad claim)
    std::string disposition;     // "corrected" | "unverifiable" | "user_downvote"
    std::string truth_summary;   // readable engine truth, e.g. "黑领先 5.5 目"
    std::string model;           // model id, when known
    // A4：用户差评补充——原因 slug（inaccurate/incomplete/harmful/formatting/other）
    // 与「其他」的自由文本说明；非差评样本时为空。
    std::string feedback_reason;
    std::string feedback_detail;
};

// A4：用户差评（👎）沉淀的坏样本处置，与 "corrected"/"unverifiable" 并列。
inline constexpr const char* kDispositionUserDownvote = "user_downvote";

// Serialize one sample as a single-line JSON object (no trailing newline).
std::string badSampleToJsonl(const BadSample& s);

// Parse a sample line back into a record. Returns false on malformed input.
bool parseBadSample(const std::string& json, BadSample* out);

}  // namespace vgo
