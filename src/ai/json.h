#pragma once

#include <string>
#include <utility>
#include <vector>

namespace vgo {

// Minimal JSON value + parser, used by the Qt-free AI harness layer (tool
// arguments, `<claims>` verification payloads, trace lines). Deliberately small:
// it only needs to read the JSON our own prompts ask the model to emit, and to
// serialize short result objects. No Qt, no I/O.
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;

    // Convenience factories for building result payloads.
    static Json makeNull() { return Json{}; }
    static Json makeBool(bool b) { Json j; j.kind = Kind::Bool; j.boolean = b; return j; }
    static Json makeNumber(double n) { Json j; j.kind = Kind::Number; j.number = n; return j; }
    static Json makeString(std::string s) { Json j; j.kind = Kind::String; j.string = std::move(s); return j; }
    static Json makeArray(std::vector<Json> a) { Json j; j.kind = Kind::Array; j.array = std::move(a); return j; }
    static Json makeObject(std::vector<std::pair<std::string, Json>> o) {
        Json j; j.kind = Kind::Object; j.object = std::move(o); return j;
    }

    bool isNull() const { return kind == Kind::Null; }
    bool isBool() const { return kind == Kind::Bool; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isString() const { return kind == Kind::String; }
    bool isArray() const { return kind == Kind::Array; }
    bool isObject() const { return kind == Kind::Object; }

    // Object member lookup (nullptr if missing or not an object).
    const Json* get(const std::string& key) const;

    double asNumber(double def = 0.0) const;
    std::string asString(const std::string& def = "") const;
    bool asBool(bool def = false) const;
};

// Parse a JSON document into `out`. Returns false on malformed input or trailing
// garbage; `out` is left in an unspecified state on failure.
bool parseJson(const std::string& text, Json* out);

// Serialize a JSON value to a compact string (used for tool results).
std::string dumpJson(const Json& v);

}  // namespace vgo
