#include "record.h"

#include <sstream>
#include <utility>

#include "gtp.h"

namespace vgo {

namespace {

// Reads the next token from `ls` into `out`. Returns false at end-of-input.
bool nextToken(std::istringstream& ls, std::string* out) {
    return static_cast<bool>(ls >> *out);
}

}  // namespace

std::string serializeRecord(const GameRecord& r) {
    std::ostringstream os;
    os << "VGO 1\n";
    os << "SIZE " << r.size << "\n";
    os << "KOMI " << r.komi << "\n";
    for (const MoveRecord& m : r.moves) {
        os << (m.color == Color::Black ? 'B' : 'W') << ' ';
        if (m.move.is_pass) {
            os << "PASS";
        } else {
            os << gtpPoint(m.move.p, r.size);
        }
        os << '\n';
    }
    for (const BlunderNote& b : r.blunders) {
        os << "# BAD " << b.move << ' '
           << (b.point.x >= 0 ? gtpPoint(b.point, r.size) : "-")
           << ' ' << b.text << '\n';
    }
    for (const std::string& n : r.notes) {
        os << "# " << n << "\n";
    }
    return os.str();
}

bool parseRecord(const std::string& text, GameRecord* out) {
    std::istringstream is(text);
    std::string line;
    bool got_header = false;
    bool got_size = false;
    GameRecord r;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#') {
            std::string rest = line.substr(1);
            if (!rest.empty() && rest[0] == ' ') rest.erase(rest.begin());
            // 结构化恶手标记：# BAD <手数> <GTP坐标> <说明>
            std::istringstream ls(rest);
            std::string tag;
            if (nextToken(ls, &tag) && tag == "BAD") {
                BlunderNote b;
                ls >> b.move;
                std::string coord;
                if (nextToken(ls, &coord) && coord != "-")
                    gtpParsePoint(coord, r.size, &b.point);
                std::string text;
                std::getline(ls, text);
                if (!text.empty() && text[0] == ' ') text.erase(text.begin());
                b.text = std::move(text);
                r.blunders.push_back(std::move(b));
            } else {
                // 其它备注（悔棋记录等）：保留供复盘展示。
                r.notes.push_back(rest);
            }
            continue;
        }

        std::istringstream ls(line);
        std::string tok;
        if (!nextToken(ls, &tok)) continue;

        if (tok == "VGO") {
            int ver = 0;
            ls >> ver;
            if (ver != 1) return false;
            got_header = true;
        } else if (tok == "SIZE") {
            ls >> r.size;
            if (r.size < 2 || r.size > 25) return false;
            got_size = true;
        } else if (tok == "KOMI") {
            ls >> r.komi;
        } else if (tok == "B" || tok == "W") {
            if (!got_size) return false;  // moves need a known board size
            const Color c = (tok == "B") ? Color::Black : Color::White;
            std::string coord;
            if (!nextToken(ls, &coord)) return false;
            Move mv;
            if (coord == "PASS" || coord == "pass") {
                mv.is_pass = true;
            } else {
                Point p;
                if (!gtpParsePoint(coord, r.size, &p)) return false;
                mv.p = p;
            }
            r.moves.push_back(MoveRecord{c, mv});
        }
        // Unknown tokens are ignored (forward-compatible).
    }
    if (!got_header) return false;
    if (out) *out = std::move(r);
    return true;
}

GameRecord recordFromGame(const Game& game) {
    GameRecord r;
    r.size = game.size();
    r.komi = game.komi();
    // 用完整着法日志（含悔棋）：仍在棋盘上的进 moves，被悔掉的进 notes，保证
    // 悔棋历史不丢失，同时最终局面由 moves 干净重放。
    for (const Game::LoggedMove& lm : game.moveLog()) {
        if (lm.active) {
            r.moves.push_back(MoveRecord{lm.color, lm.move});
        } else {
            const std::string coord =
                lm.move.is_pass ? std::string("虚着") : gtpPoint(lm.move.p, r.size);
            r.notes.push_back("悔棋：第 " + std::to_string(lm.number) + " 手 " +
                              (lm.color == Color::Black ? "黑" : "白") + " " + coord +
                              "（已撤销）");
        }
    }
    return r;
}

Game recordToGame(const GameRecord& r, int reveal) {
    Game g(r.size, r.komi);
    if (reveal < 0) reveal = 0;
    const int n = static_cast<int>(r.moves.size());
    if (reveal > n) reveal = n;
    for (int i = 0; i < reveal; ++i) {
        const MoveRecord& m = r.moves[static_cast<std::size_t>(i)];
        if (m.move.is_pass) {
            g.pass();
        } else {
            g.play(m.move.p.x, m.move.p.y);
        }
    }
    return g;
}

}  // namespace vgo
