#pragma once

#include <cstdint>
#include <string>

#include "../go/board.h"

namespace vgo {

// One agent-loop step, serialized to a JSONL trace line (D1.6 / D5 observability).
// The trace is the evidence base for offline evaluation: {position hash, route,
// steps, latency, verification result, cost, model}.
struct AgentTraceRecord {
    std::int64_t ts_ms = 0;        // wall-clock timestamp (ms since epoch)
    std::string position_hash;     // deterministic hash of the position
    int step = 0;                  // 1-based loop step, or 0 for a final answer
    std::string tool;              // tool name ("" for a final answer)
    std::string args;              // tool args JSON ("" for a final answer)
    int latency_ms = 0;            // wall-clock ms consumed so far
    std::string route;             // "tools" / "dispatch" / "template"
    std::string model;             // model id, when known
    std::string verify;            // D2.5/D5.6: "pass" / "corrected" / "unverifiable" / "none"
};

// Deterministic FNV-1a (64-bit) hash of the board snapshot, hex-encoded. Used as
// the position key so traces can be joined across a game without storing boards.
std::string positionHash(const Board& board);

// Serialize one record as a single-line JSON object (no trailing newline).
std::string traceToJsonl(const AgentTraceRecord& r);

// Parse a trace line back into a record. Returns false on malformed input.
bool parseTraceRecord(const std::string& json, AgentTraceRecord* out);

}  // namespace vgo
