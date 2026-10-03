#include "tool_registry.h"

#include <cstdio>

namespace vgo {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace

bool ToolRegistry::registerTool(const Tool& t) {
    if (t.name.empty()) return false;
    for (Tool& e : tools_) {
        if (e.name == t.name) {
            e = t;
            return true;
        }
    }
    tools_.push_back(t);
    return true;
}

const Tool* ToolRegistry::find(const std::string& name) const {
    for (const Tool& t : tools_)
        if (t.name == name) return &t;
    return nullptr;
}

std::string ToolRegistry::schemasForModel() const {
    std::string out = "[";
    bool first = true;
    for (const Tool& t : tools_) {
        if (!first) out += ",";
        first = false;
        out += "{\"type\":\"function\",\"function\":{";
        out += "\"name\":\"" + jsonEscape(t.name) + "\",";
        out += "\"description\":\"" + jsonEscape(t.description) + "\",";
        out += "\"parameters\":" + t.json_schema;  // already a JSON object
        out += "}}";
    }
    out += "]";
    return out;
}

}  // namespace vgo
