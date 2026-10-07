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
    std::string disposition;     // "corrected" | "unverifiable"
    std::string truth_summary;   // readable engine truth, e.g. "黑领先 5.5 目"
    std::string model;           // model id, when known
};

// Serialize one sample as a single-line JSON object (no trailing newline).
std::string badSampleToJsonl(const BadSample& s);

// Parse a sample line back into a record. Returns false on malformed input.
bool parseBadSample(const std::string& json, BadSample* out);

}  // namespace vgo
