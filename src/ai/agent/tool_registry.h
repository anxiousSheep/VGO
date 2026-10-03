#pragma once

#include <string>
#include <vector>

#include "tool.h"

namespace vgo {

// A named set of tools. Pure data + lookup; no Qt, no I/O. D1's "Tool Registry":
// capabilities become visible to the model through their schemas, and dispatch
// goes through find()/invoke rather than a hard-coded keyword classifier.
class ToolRegistry {
public:
    // Adds or replaces a tool by name. Returns false if the name is empty.
    bool registerTool(const Tool& t);

    const Tool* find(const std::string& name) const;
    const std::vector<Tool>& all() const { return tools_; }
    std::size_t size() const { return tools_.size(); }
    void clear() { tools_.clear(); }

    // All tool schemas as a single JSON array in the OpenAI `tools` shape:
    // [{"type":"function","function":{"name":..,"description":..,"parameters":..}}, …]
    std::string schemasForModel() const;

private:
    std::vector<Tool> tools_;
};

}  // namespace vgo
