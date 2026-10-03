#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vgo {

namespace {

struct Parser {
    const std::string& s;
    std::size_t i = 0;

    explicit Parser(const std::string& str) : s(str) {}

    void skipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                                s[i] == '\r'))
            ++i;
    }

    bool expect(char c) {
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }

    bool consume(const char* lit) {
        std::size_t n = 0;
        while (lit[n]) ++n;
        if (i + n <= s.size() && s.compare(i, n, lit) == 0) {
            i += n;
            return true;
        }
        return false;
    }

    bool parseString(std::string* out) {
        if (!expect('"')) return false;
        std::string r;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') {
                *out = std::move(r);
                return true;
            }
            if (c != '\\') {
                r += c;
                continue;
            }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case '"': r += '"'; break;
                case '\\': r += '\\'; break;
                case '/': r += '/'; break;
                case 'b': r += '\b'; break;
                case 'f': r += '\f'; break;
                case 'n': r += '\n'; break;
                case 'r': r += '\r'; break;
                case 't': r += '\t'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        int d;
                        if (h >= '0' && h <= '9') d = h - '0';
                        else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                        else return false;
                        cp = cp * 16 + static_cast<unsigned>(d);
                    }
                    // Encode UTF-8 for the BMP (adequate for our payloads).
                    if (cp < 0x80) {
                        r += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        r += static_cast<char>(0xC0 | (cp >> 6));
                        r += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        r += static_cast<char>(0xE0 | (cp >> 12));
                        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        r += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;  // unterminated string
    }

    bool parseNumber(double* out) {
        std::size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i == start) return false;
        *out = std::strtod(s.c_str() + start, nullptr);
        return true;
    }

    bool parseObject(Json* out) {
        if (!expect('{')) return false;
        std::vector<std::pair<std::string, Json>> obj;
        skipWs();
        if (expect('}')) {
            out->kind = Json::Kind::Object;
            out->object = std::move(obj);
            return true;
        }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(&key)) return false;
            skipWs();
            if (!expect(':')) return false;
            skipWs();
            Json v;
            if (!parseValue(&v)) return false;
            obj.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (expect('}')) break;
            if (!expect(',')) return false;
        }
        out->kind = Json::Kind::Object;
        out->object = std::move(obj);
        return true;
    }

    bool parseArray(Json* out) {
        if (!expect('[')) return false;
        std::vector<Json> arr;
        skipWs();
        if (expect(']')) {
            out->kind = Json::Kind::Array;
            out->array = std::move(arr);
            return true;
        }
        while (true) {
            skipWs();
            Json v;
            if (!parseValue(&v)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (expect(']')) break;
            if (!expect(',')) return false;
        }
        out->kind = Json::Kind::Array;
        out->array = std::move(arr);
        return true;
    }

    bool parseValue(Json* out) {
        skipWs();
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string str;
            if (!parseString(&str)) return false;
            out->kind = Json::Kind::String;
            out->string = std::move(str);
            return true;
        }
        if (consume("true")) { out->kind = Json::Kind::Bool; out->boolean = true; return true; }
        if (consume("false")) { out->kind = Json::Kind::Bool; out->boolean = false; return true; }
        if (consume("null")) { out->kind = Json::Kind::Null; return true; }
        double n = 0.0;
        if (parseNumber(&n)) { out->kind = Json::Kind::Number; out->number = n; return true; }
        return false;
    }
};

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

void dump(const Json& v, std::string* out) {
    switch (v.kind) {
        case Json::Kind::Null: *out += "null"; break;
        case Json::Kind::Bool: *out += v.boolean ? "true" : "false"; break;
        case Json::Kind::Number: {
            char buf[64];
            if (std::isfinite(v.number) && v.number == std::floor(v.number) &&
                std::abs(v.number) < 1e15) {
                std::snprintf(buf, sizeof(buf), "%.0f", v.number);
            } else {
                std::snprintf(buf, sizeof(buf), "%.4f", v.number);
            }
            *out += buf;
            break;
        }
        case Json::Kind::String: *out += "\"" + escapeString(v.string) + "\""; break;
        case Json::Kind::Array: {
            *out += "[";
            for (std::size_t i = 0; i < v.array.size(); ++i) {
                if (i) *out += ",";
                dump(v.array[i], out);
            }
            *out += "]";
            break;
        }
        case Json::Kind::Object: {
            *out += "{";
            bool first = true;
            for (const auto& kv : v.object) {
                if (!first) *out += ",";
                first = false;
                *out += "\"" + escapeString(kv.first) + "\":";
                dump(kv.second, out);
            }
            *out += "}";
            break;
        }
    }
}

}  // namespace

const Json* Json::get(const std::string& key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& kv : object)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

double Json::asNumber(double def) const {
    return kind == Kind::Number ? number : def;
}

std::string Json::asString(const std::string& def) const {
    return kind == Kind::String ? string : def;
}

bool Json::asBool(bool def) const {
    return kind == Kind::Bool ? boolean : def;
}

bool parseJson(const std::string& text, Json* out) {
    if (!out) return false;
    Parser p(text);
    Json v;
    if (!p.parseValue(&v)) return false;
    p.skipWs();
    if (p.i != text.size()) return false;  // trailing garbage
    *out = std::move(v);
    return true;
}

std::string dumpJson(const Json& v) {
    std::string out;
    dump(v, &out);
    return out;
}

}  // namespace vgo
