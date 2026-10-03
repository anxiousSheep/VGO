#include "sgf.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace vgo {

namespace {

// SGF property values escape '\' and ']'.
std::string escapeSgf(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if (c == '\\' || c == ']') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// Formats a komi value without a trailing ".0" (e.g. 7.5 -> "7.5", 7 -> "7").
std::string formatKomi(double komi) {
    std::ostringstream os;
    os << komi;
    return os.str();
}

// True when a move value denotes a pass: empty, the literal "pass", or "tt"
// (a common non-standard pass spelling; "tt" is off-board for any board <= 19).
bool isPassValue(const std::string& v) {
    if (v.empty() || v == "pass" || v == "PASS") return true;
    if (v == "tt" || v == "TT") return true;
    return false;
}

// Appends a single move property to `r`. `size` is the board size used to
// resolve the coordinate. Returns false if the coordinate is invalid.
bool applyMove(GameRecord* r, const std::string& id, const std::string& value,
               int size) {
    const Color c = (id == "B") ? Color::Black : Color::White;
    Move mv;
    if (isPassValue(value)) {
        mv.is_pass = true;
    } else {
        Point p;
        if (!sgfParsePoint(value, size, &p)) return false;
        mv.p = p;
    }
    r->moves.push_back(MoveRecord{c, mv});
    return true;
}

// Parses a "VGO-BAD:<move>:<coord>:<text>" comment back into a BlunderNote.
// The text may itself contain ':' — everything after the third separator is
// treated as the text.
void applyBadComment(GameRecord* r, const std::string& value, int size) {
    // value starts with "VGO-BAD:"
    const std::string body = value.substr(std::string("VGO-BAD:").size());
    const std::size_t sep1 = body.find(':');
    if (sep1 == std::string::npos) return;
    const std::size_t sep2 = body.find(':', sep1 + 1);
    if (sep2 == std::string::npos) return;

    BlunderNote b;
    b.move = std::atoi(body.substr(0, sep1).c_str());
    const std::string coord = body.substr(sep1 + 1, sep2 - sep1 - 1);
    if (coord != "-") sgfParsePoint(coord, size, &b.point);
    b.text = body.substr(sep2 + 1);
    r->blunders.push_back(std::move(b));
}

}  // namespace

std::string sgfPoint(const Point& p) {
    std::string s;
    s.push_back(static_cast<char>('a' + p.x));
    s.push_back(static_cast<char>('a' + p.y));
    return s;
}

bool sgfParsePoint(const std::string& s, int size, Point* out) {
    if (s.size() != 2) return false;
    const int x = std::tolower(static_cast<unsigned char>(s[0])) - 'a';
    const int y = std::tolower(static_cast<unsigned char>(s[1])) - 'a';
    if (x < 0 || x >= size || y < 0 || y >= size) return false;
    if (out) *out = {x, y};
    return true;
}

std::string serializeSgf(const GameRecord& r) {
    std::ostringstream os;
    os << "(;GM[1]FF[4]CA[UTF-8]AP[VGO:1.0]SZ[" << r.size << "]KM["
       << formatKomi(r.komi) << "]\n";
    for (const BlunderNote& b : r.blunders) {
        const std::string coord =
            (b.point.x >= 0) ? sgfPoint(b.point) : std::string("-");
        os << "C[VGO-BAD:" << b.move << ':' << coord << ':'
           << escapeSgf(b.text) << "]\n";
    }
    for (const std::string& note : r.notes) {
        os << "C[" << escapeSgf(note) << "]\n";
    }
    for (const MoveRecord& m : r.moves) {
        os << ';' << (m.color == Color::Black ? 'B' : 'W') << '[';
        if (!m.move.is_pass) os << sgfPoint(m.move.p);
        os << "]\n";
    }
    os << ")\n";
    return os.str();
}

bool parseSgf(const std::string& text, GameRecord* out) {
    GameRecord r;
    r.size = 19;  // SGF default board size (used until SZ overrides it)

    const std::size_t n = text.size();
    std::size_t i = 0;

    auto skipWs = [&]() {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    };

    skipWs();
    if (i >= n || text[i] != '(') return false;
    ++i;
    skipWs();
    if (i >= n || text[i] != ';') return false;
    ++i;  // now at the root node's properties

    // Skips a whole variation subtree. On entry text[i] == '('.
    auto skipSubTree = [&]() {
        int depth = 0;
        while (i < n) {
            const char c = text[i];
            if (c == '\\') {  // skip the escaped char
                i += 2;
                continue;
            }
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
                if (depth == 0) {
                    ++i;
                    return;
                }
            }
            ++i;
        }
    };

    // Parses one node's properties. On entry `i` is just after ';'. Leaves `i`
    // at the next delimiter (';', '(' or ')') or fails on malformed input.
    auto parseNode = [&]() -> bool {
        for (;;) {
            skipWs();
            if (i >= n) return false;
            const char c = text[i];
            if (c == ';' || c == '(' || c == ')') return true;  // node boundary

            // Property identifier: one or more uppercase letters.
            if (c < 'A' || c > 'Z') return false;
            const std::size_t id_start = i;
            while (i < n && text[i] >= 'A' && text[i] <= 'Z') ++i;
            const std::string id = text.substr(id_start, i - id_start);
            skipWs();

            // One or more [value]s. We only use the first value for B/W/SZ/KM/C.
            std::vector<std::string> values;
            while (i < n && text[i] == '[') {
                ++i;
                std::string v;
                while (i < n && text[i] != ']') {
                    if (text[i] == '\\' && i + 1 < n) {
                        ++i;
                        v.push_back(text[i]);
                        ++i;
                        continue;
                    }
                    v.push_back(text[i]);
                    ++i;
                }
                if (i >= n) return false;  // unterminated value
                ++i;                       // skip ']'
                values.push_back(std::move(v));
            }
            if (values.empty()) return false;  // property with no value

            if (id == "SZ") {
                r.size = std::atoi(values[0].c_str());
                if (r.size < 2 || r.size > 25) return false;
            } else if (id == "KM") {
                r.komi = std::atof(values[0].c_str());
            } else if (id == "B" || id == "W") {
                if (!applyMove(&r, id, values[0], r.size)) return false;
            } else if (id == "C") {
                // values[0] is already unescaped by the value reader above.
                const std::string& v = values[0];
                if (v.rfind("VGO-BAD:", 0) == 0) {
                    applyBadComment(&r, v, r.size);
                } else {
                    r.notes.push_back(v);
                }
            }
            // Unknown properties are ignored (forward-compatible).
        }
    };

    if (!parseNode()) return false;

    // Mainline: read sibling nodes until the tree closes. ';' continues the
    // mainline; '(' opens a variation subtree (skipped); ')' ends the tree.
    for (;;) {
        skipWs();
        if (i >= n) return false;  // unterminated tree
        const char c = text[i];
        if (c == ')') {
            ++i;
            break;
        }
        if (c == ';') {
            ++i;
            if (!parseNode()) return false;
        } else if (c == '(') {
            skipSubTree();
        } else {
            return false;
        }
    }

    if (out) *out = std::move(r);
    return true;
}

}  // namespace vgo
