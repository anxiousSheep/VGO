#include "gtp.h"

#include <cctype>

namespace vgo {

namespace {

char colLetter(int x) {
    int c = x + 'A';
    if (c >= 'I') ++c;  // skip 'I'
    return static_cast<char>(c);
}

int colIndex(char c) {
    int x = std::toupper(static_cast<unsigned char>(c)) - 'A';
    if (x >= 8) --x;  // 'I' is skipped, so letters after it shift down by one
    return x;
}

}  // namespace

std::string gtpPoint(int x, int y, int size) {
    std::string s;
    s.push_back(colLetter(x));
    s += std::to_string(size - y);  // y = 0 (top) -> row = size
    return s;
}

std::string gtpPoint(const Point& p, int size) {
    return gtpPoint(p.x, p.y, size);
}

bool gtpParsePoint(const std::string& s, int size, Point* out) {
    if (s.size() < 2) return false;
    const char col = std::toupper(static_cast<unsigned char>(s[0]));
    if (col < 'A' || col > 'Z' || col == 'I') return false;
    const int x = colIndex(col);
    if (x < 0 || x >= size) return false;

    int row = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        const char ch = s[i];
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        row = row * 10 + (ch - '0');
    }
    const int y = size - row;
    if (y < 0 || y >= size) return false;
    if (out) *out = {x, y};
    return true;
}

}  // namespace vgo
