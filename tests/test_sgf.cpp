#include "test_framework.h"

#include <cmath>

#include "ai/sgf.h"

using namespace vgo;

TEST(sgf_point_encode) {
    CHECK_EQ(sgfPoint(Point{0, 0}), "aa");    // top-left
    CHECK_EQ(sgfPoint(Point{15, 3}), "pd");   // top-right 4-4 on 19x19
    CHECK_EQ(sgfPoint(Point{3, 15}), "dp");   // bottom-left 4-4
    CHECK_EQ(sgfPoint(Point{18, 18}), "ss");  // bottom-right
}

TEST(sgf_point_parse) {
    const int size = 19;
    Point p;
    CHECK(sgfParsePoint("aa", size, &p));
    CHECK_EQ(p.x, 0);
    CHECK_EQ(p.y, 0);
    CHECK(sgfParsePoint("pd", size, &p));
    CHECK_EQ(p.x, 15);
    CHECK_EQ(p.y, 3);
    CHECK(sgfParsePoint("SS", size, &p));  // case-insensitive
    CHECK_EQ(p.x, 18);
    CHECK_EQ(p.y, 18);

    CHECK(!sgfParsePoint("", size, &p));     // wrong length
    CHECK(!sgfParsePoint("a", size, &p));    // wrong length
    CHECK(!sgfParsePoint("abc", size, &p));  // wrong length
    CHECK(!sgfParsePoint("zz", 9, &p));      // out of range on 9x9
    CHECK(!sgfParsePoint("tt", 19, &p));     // off-board on 19x19
}

TEST(sgf_roundtrip) {
    GameRecord r;
    r.size = 9;
    r.komi = 7.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{3, 5}, false}});   // dd-ish
    r.moves.push_back(MoveRecord{Color::White, Move{{2, 6}, false}});
    r.moves.push_back(MoveRecord{Color::Black, Move{{-1, -1}, true}});  // pass

    const std::string s = serializeSgf(r);
    CHECK(s.find("(;GM[1]FF[4]") != std::string::npos);
    CHECK(s.find("SZ[9]") != std::string::npos);
    CHECK(s.find("KM[7.5]") != std::string::npos);

    GameRecord p;
    CHECK(parseSgf(s, &p));
    CHECK_EQ(p.size, 9);
    CHECK(std::abs(p.komi - 7.5) < 1e-9);
    CHECK_EQ(p.moves.size(), 3u);
    CHECK(p.moves[0].color == Color::Black);
    CHECK_EQ(p.moves[0].move.p.x, 3);
    CHECK_EQ(p.moves[0].move.p.y, 5);
    CHECK(p.moves[1].color == Color::White);
    CHECK(p.moves[2].move.is_pass);
}

TEST(sgf_parse_standard_opening) {
    // Canonical 19x19 opening. Verifies the top-left origin and column/row order:
    // 'pd' = column 15, row 3 (top-right 4-4); 'dp' = bottom-left 4-4.
    const std::string s =
        "(;GM[1]FF[4]SZ[19]KM[6.5]PB[Black]PW[White]\n"
        ";B[pd]\n"
        ";W[dp]\n"
        ";B[pp])\n";
    GameRecord r;
    CHECK(parseSgf(s, &r));
    CHECK_EQ(r.size, 19);
    CHECK(std::abs(r.komi - 6.5) < 1e-9);
    CHECK_EQ(r.moves.size(), 3u);
    CHECK_EQ(r.moves[0].move.p.x, 15);
    CHECK_EQ(r.moves[0].move.p.y, 3);
    CHECK_EQ(r.moves[1].move.p.x, 3);
    CHECK_EQ(r.moves[1].move.p.y, 15);
    CHECK_EQ(r.moves[2].move.p.x, 15);
    CHECK_EQ(r.moves[2].move.p.y, 15);
}

TEST(sgf_pass_variants) {
    GameRecord r;
    CHECK(parseSgf("(;FF[4]SZ[19];B[];W[];B[tt])", &r));
    CHECK_EQ(r.moves.size(), 3u);
    CHECK(r.moves[0].move.is_pass);
    CHECK(r.moves[1].move.is_pass);
    CHECK(r.moves[2].move.is_pass);  // "tt" treated as pass
}

TEST(sgf_ignores_variations) {
    // A mainline plus a variation subtree; only the mainline is extracted.
    const std::string s =
        "(;FF[4]SZ[9]\n"
        ";B[dd]\n"
        "(;W[ee];B[ff])\n"   // variation — skipped
        ";W[gg]\n"
        ";B[cc])\n";
    GameRecord r;
    CHECK(parseSgf(s, &r));
    CHECK_EQ(r.moves.size(), 3u);
    CHECK(r.moves[0].move.p.x == 3 && r.moves[0].move.p.y == 3);   // dd
    CHECK(r.moves[1].move.p.x == 6 && r.moves[1].move.p.y == 6);   // gg
    CHECK(r.moves[2].move.p.x == 2 && r.moves[2].move.p.y == 2);   // cc
}

TEST(sgf_blunders_notes_roundtrip) {
    GameRecord r;
    r.size = 9;
    r.komi = 7.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{4, 4}, false}});  // ee
    BlunderNote b;
    b.move = 3;
    b.point = {4, 4};
    b.text = "这步明显欠妥: 胜率掉 20%";
    r.blunders.push_back(b);
    r.notes.push_back("备注含]右括号[和\\反斜杠");

    const std::string s = serializeSgf(r);
    GameRecord p;
    CHECK(parseSgf(s, &p));
    CHECK_EQ(p.moves.size(), 1u);
    CHECK_EQ(p.blunders.size(), 1u);
    CHECK_EQ(p.blunders[0].move, 3);
    CHECK_EQ(p.blunders[0].point.x, 4);
    CHECK_EQ(p.blunders[0].point.y, 4);
    CHECK(p.blunders[0].text == "这步明显欠妥: 胜率掉 20%");
    CHECK_EQ(p.notes.size(), 1u);
    CHECK(p.notes[0] == "备注含]右括号[和\\反斜杠");
}

TEST(sgf_parse_invalid) {
    GameRecord r;
    CHECK(!parseSgf("", &r));
    CHECK(!parseSgf("not sgf", &r));
    CHECK(!parseSgf(";B[dd])", &r));          // no opening '('
    CHECK(!parseSgf("(;FF[4]SZ[9];B[dd]", &r));  // unterminated
    CHECK(!parseSgf("(;FF[4]SZ[9];B[d)", &r));   // malformed move
    CHECK(!parseSgf("(;FF[4]SZ[99];B[dd])", &r));  // bad board size
}
