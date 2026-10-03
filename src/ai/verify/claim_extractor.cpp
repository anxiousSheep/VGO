#include "claim_extractor.h"

#include <cctype>
#include <cstdlib>
#include <string>

#include "../evaluate.h"  // findPointInQuestion
#include "../gtp.h"
#include "../json.h"

namespace vgo {

namespace {

Color colorFromString(const std::string& s) {
    if (s == "black") return Color::Black;
    if (s == "white") return Color::White;
    return Color::Empty;
}

// Whether `text` contains a life ("活") or death ("死") hint within `span` chars
// after `pos`. Returns 0 = none, 1 = alive, 2 = dead.
int lifeHintNear(const std::string& text, std::size_t pos, std::size_t span) {
    const std::size_t end = std::min(text.size(), pos + span);
    for (std::size_t i = pos; i < end; ++i) {
        if (text[i] == '\xe6' && i + 2 < end) {  // UTF-8 3-byte CJK lead byte
            // 死 = E6 AD BB, 活 = E6 B4 BB, 做 = E5 81 9A
            const std::string tri = text.substr(i, 3);
            if (tri == "\xe6\xad\xbb") return 2;  // 死
            if (tri == "\xe6\xb4\xbb") return 1;  // 活
        }
    }
    return 0;
}

// Whether a CJK char (or ASCII token) for "alive"/"dead" appears near a point.
// Simplified: just scan the whole text for 死/活 and return the nearest verdict.
int lifeVerdict(const std::string& text) {
    std::size_t dead = text.find("\xe6\xad\xbb");  // 死
    std::size_t alive = text.find("\xe6\xb4\xbb");  // 活
    if (dead == std::string::npos && alive == std::string::npos) return 0;
    if (dead == std::string::npos) return 1;
    if (alive == std::string::npos) return 2;
    return dead < alive ? 2 : 1;
}

// Scans a floating-point number at/after `from` (skipping a short run of
// separator chars like "约"/"是"/"为"/spaces). Returns false if none found.
bool scanNumber(const std::string& s, std::size_t from, double* val,
                std::size_t* end) {
    const std::size_t limit = std::min(s.size(), from + 12);
    std::size_t i = from;
    while (i < limit && !std::isdigit(static_cast<unsigned char>(s[i])) &&
           s[i] != '.' && s[i] != '-')
        ++i;
    if (i >= s.size()) return false;
    const std::size_t start = i;
    bool seenDigit = false;
    bool seenDot = false;
    if (s[i] == '-') ++i;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            seenDigit = true;
            ++i;
        } else if (c == '.' && !seenDot) {
            seenDot = true;
            ++i;
        } else {
            break;
        }
    }
    if (!seenDigit) return false;
    *val = std::strtod(s.c_str() + start, nullptr);
    *end = i;
    return true;
}

// The side mentioned just before `pos` ("黑"/"白"), or Empty if ambiguous.
Color colorBefore(const std::string& text, std::size_t pos) {
    const std::size_t start = (pos > 24) ? pos - 24 : 0;
    Color c = Color::Empty;
    for (std::size_t i = start; i < pos; ++i) {
        if (i + 2 >= text.size()) break;
        const std::string tri = text.substr(i, 3);
        if (tri == "\xe9\xbb\x91") c = Color::Black;  // 黑
        else if (tri == "\xe7\x99\xbd") c = Color::White;  // 白
    }
    return c;
}

}  // namespace

std::vector<Claim> ClaimExtractor::fromClaimsBlock(const std::string& text,
                                                   int size) const {
    std::vector<Claim> out;
    const std::size_t open = text.find("<claims>");
    if (open == std::string::npos) return out;
    const std::size_t close = text.find("</claims>", open);
    if (close == std::string::npos) return out;
    const std::string payload = text.substr(open + 8, close - open - 8);

    Json j;
    if (!parseJson(payload, &j) || !j.isArray()) return out;
    for (const Json& item : j.array) {
        if (!item.isObject()) continue;
        const Json* type = item.get("type");
        if (!type || !type->isString()) continue;
        const std::string t = type->string;
        Claim c;
        c.valid = true;
        if (t == "score_lead") {
            c.type = ClaimType::ScoreLead;
            c.value = item.get("value") ? item.get("value")->asNumber() : 0.0;
            c.color = colorFromString(item.get("color") ? item.get("color")->asString() : "");
        } else if (t == "winrate") {
            c.type = ClaimType::Winrate;
            c.value = item.get("value") ? item.get("value")->asNumber() : 0.0;
            c.color = colorFromString(item.get("color") ? item.get("color")->asString() : "");
        } else if (t == "group_life") {
            c.type = ClaimType::GroupLife;
            if (item.get("point"))
                gtpParsePoint(item.get("point")->asString(), size, &c.point);
            else
                c.point = {-1, -1};
            c.alive = item.get("alive") ? item.get("alive")->asBool(true) : true;
        } else {
            c.valid = false;
            continue;
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<Claim> ClaimExtractor::fromProse(const std::string& text, int size) const {
    std::vector<Claim> out;

    // Winrate: "胜率 62%" / "胜率62%".
    std::size_t pos = 0;
    while ((pos = text.find("\xe8\x83\x9c\xe7\x8e\x87", pos)) !=  // 胜率
           std::string::npos) {
        double v = 0.0;
        std::size_t end = 0;
        if (scanNumber(text, pos + 6, &v, &end)) {
            Claim c;
            c.type = ClaimType::Winrate;
            c.value = v / 100.0;  // percent -> [0,1]
            c.color = colorBefore(text, pos);
            c.valid = true;
            out.push_back(std::move(c));
        }
        pos += 6;
    }

    // Score lead: "黑领先 5.5 目" / "领先约 3 目".
    pos = 0;
    while ((pos = text.find("\xe9\xa2\x86\xe5\x85\x88", pos)) !=  // 领先
           std::string::npos) {
        double v = 0.0;
        std::size_t end = 0;
        if (scanNumber(text, pos + 6, &v, &end)) {
            Claim c;
            c.type = ClaimType::ScoreLead;
            c.value = v;
            c.color = colorBefore(text, pos);
            c.valid = true;
            out.push_back(std::move(c));
        }
        pos += 6;
    }

    // Group life: a coordinate plus an alive/dead hint.
    const PointMatch pm = findPointInQuestion(text, size);
    if (pm.found) {
        const int verdict = lifeVerdict(text);
        if (verdict != 0) {
            Claim c;
            c.type = ClaimType::GroupLife;
            c.point = pm.p;
            c.alive = (verdict == 1);
            c.valid = true;
            out.push_back(std::move(c));
        }
    }
    return out;
}

std::vector<Claim> ClaimExtractor::extract(const std::string& text, int size) const {
    std::vector<Claim> structured = fromClaimsBlock(text, size);
    if (!structured.empty()) return structured;
    return fromProse(text, size);
}

}  // namespace vgo
