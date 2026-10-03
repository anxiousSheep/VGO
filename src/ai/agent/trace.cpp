#include "trace.h"

#include <cstdio>

#include "../json.h"

namespace vgo {

std::string positionHash(const Board& board) {
    const std::string snap = board.snapshot();
    std::uint64_t h = 1469598103934665603ULL;  // FNV offset basis
    for (unsigned char c : snap) {
        h ^= c;
        h *= 1099511628211ULL;  // FNV prime
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return buf;
}

std::string traceToJsonl(const AgentTraceRecord& r) {
    return dumpJson(Json::makeObject({
        {"ts", Json::makeNumber(static_cast<double>(r.ts_ms))},
        {"position_hash", Json::makeString(r.position_hash)},
        {"step", Json::makeNumber(r.step)},
        {"tool", Json::makeString(r.tool)},
        {"args", Json::makeString(r.args)},
        {"latency_ms", Json::makeNumber(r.latency_ms)},
        {"route", Json::makeString(r.route)},
        {"model", Json::makeString(r.model)},
    }));
}

}  // namespace vgo
