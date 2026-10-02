#include "test_framework.h"

#include <cmath>

#include "ai/record.h"

using namespace vgo;

TEST(record_roundtrip) {
    GameRecord r;
    r.size = 9;
    r.komi = 7.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{3, 5}, false}});   // D4
    r.moves.push_back(MoveRecord{Color::White, Move{{2, 6}, false}});   // C3
    r.moves.push_back(MoveRecord{Color::Black, Move{{-1, -1}, true}});  // pass

    const std::string s = serializeRecord(r);
    GameRecord p;
    CHECK(parseRecord(s, &p));
    CHECK_EQ(p.size, 9);
    CHECK(std::abs(p.komi - 7.5) < 1e-9);
    CHECK_EQ(p.moves.size(), 3u);
    CHECK(p.moves[0].color == Color::Black);
    CHECK_EQ(p.moves[0].move.p.x, 3);
    CHECK_EQ(p.moves[0].move.p.y, 5);
    CHECK(p.moves[1].color == Color::White);
    CHECK(p.moves[2].move.is_pass);
}

TEST(record_serialize_shape) {
    GameRecord r;
    r.size = 9;
    r.komi = 6.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{4, 4}, false}});  // E5
    const std::string s = serializeRecord(r);
    CHECK(s.find("VGO 1") != std::string::npos);
    CHECK(s.find("SIZE 9") != std::string::npos);
    CHECK(s.find("KOMI 6.5") != std::string::npos);
    CHECK(s.find("B E5") != std::string::npos);
}

TEST(record_from_game) {
    Game g(9);
    g.play(4, 4);  // black E5
    g.play(2, 4);  // white C5
    g.pass();      // black pass

    GameRecord r = recordFromGame(g);
    CHECK_EQ(r.size, 9);
    CHECK_EQ(r.moves.size(), 3u);
    CHECK(r.moves[0].color == Color::Black);
    CHECK_EQ(r.moves[0].move.p.x, 4);
    CHECK_EQ(r.moves[0].move.p.y, 4);
    CHECK(r.moves[1].color == Color::White);
    CHECK(r.moves[2].move.is_pass);
}

TEST(record_to_game_reveal) {
    GameRecord r;
    r.size = 9;
    r.komi = 7.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{4, 4}, false}});  // E5
    r.moves.push_back(MoveRecord{Color::White, Move{{2, 4}, false}});  // C5

    Game g1 = recordToGame(r, 1);
    CHECK_EQ(g1.move_number(), 1);
    CHECK(g1.board().at(4, 4) == Color::Black);
    CHECK(g1.current() == Color::White);

    Game g2 = recordToGame(r, 2);
    CHECK_EQ(g2.move_number(), 2);
    CHECK(g2.board().at(2, 4) == Color::White);
    CHECK(g2.current() == Color::Black);

    // Clamp out-of-range reveal.
    Game g0 = recordToGame(r, -5);
    CHECK_EQ(g0.move_number(), 0);
    Game g3 = recordToGame(r, 99);
    CHECK_EQ(g3.move_number(), 2);
}

TEST(record_parse_invalid) {
    GameRecord r;
    CHECK(!parseRecord("not a vgo file\n", &r));          // no header
    CHECK(!parseRecord("VGO 2\nSIZE 9\n", &r));           // bad version
    CHECK(!parseRecord("VGO 1\nB D4\n", &r));             // move before SIZE
    CHECK(!parseRecord("VGO 1\nSIZE 9\nB Z9\n", &r));     // bad column
    CHECK(!parseRecord("VGO 1\nSIZE 9\nB D99\n", &r));    // out of range
    CHECK(!parseRecord("VGO 1\nSIZE 40\n", &r));          // bad board size
}
