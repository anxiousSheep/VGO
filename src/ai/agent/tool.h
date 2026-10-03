#pragma once

#include <functional>
#include <string>

namespace vgo {

// Result of invoking a tool. `ok == false` means the tool could not run (e.g.
// missing engine or illegal argument); `error` then carries a short reason that
// is safe to feed back to the model.
struct ToolResult {
    bool ok = true;
    std::string json;    // result payload (JSON string) fed back to the model
    std::string error;   // populated when ok == false
};

// A model-callable tool: a natural-language description, a JSON Schema for its
// arguments, and a synchronous pure-logic invoker. Tools wrap the *existing*
// deterministic capabilities (MoveEvaluator / LifeDeathAnalyzer / ownership /
// rules scoring); they never implement new algorithms. No Qt, no I/O.
struct Tool {
    std::string name;
    std::string description;
    std::string json_schema;  // JSON Schema object (as a string) for the params
    std::function<ToolResult(const std::string& args_json)> invoke;
};

}  // namespace vgo
