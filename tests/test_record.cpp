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

TEST(record_undo_history_as_note) {
    Game g(9);
    g.play(4, 4);  // 黑 E5（第 1 手）
    g.play(2, 4);  // 白 C5（第 2 手，稍后悔棋）
    g.undo();
    g.play(3, 4);  // 白 D5（第 3 手，替换被悔的一手）

    GameRecord r = recordFromGame(g);
    // 最终局面只有 2 手；被悔的白 C5 进备注。
    CHECK_EQ(r.moves.size(), 2u);
    CHECK_EQ(r.moves[0].move.p.x, 4);
    CHECK_EQ(r.moves[1].move.p.x, 3);
    CHECK_EQ(r.notes.size(), 1u);
    CHECK(r.notes[0].find("悔棋") != std::string::npos);
    CHECK(r.notes[0].find("C5") != std::string::npos);
}

TEST(record_notes_roundtrip) {
    GameRecord r;
    r.size = 9;
    r.komi = 7.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{4, 4}, false}});
    r.notes.push_back("本局恶手（共 1 次）：");
    r.notes.push_back("第 3 手：这步棋明显欠妥");

    const std::string s = serializeRecord(r);
    CHECK(s.find("# 本局恶手（共 1 次）：") != std::string::npos);

    GameRecord p;
    CHECK(parseRecord(s, &p));
    CHECK_EQ(p.notes.size(), 2u);
    CHECK(p.notes[0] == "本局恶手（共 1 次）：");
    CHECK(p.notes[1] == "第 3 手：这步棋明显欠妥");
    CHECK_EQ(p.moves.size(), 1u);  // 备注不影响手数解析
}

TEST(record_blunders_roundtrip) {
    GameRecord r;
    r.size = 9;
    r.komi = 7.5;
    r.moves.push_back(MoveRecord{Color::Black, Move{{4, 4}, false}});  // E5
    BlunderNote b;
    b.move = 3;
    b.point = {4, 4};
    b.text = "这步棋明显欠妥";
    r.blunders.push_back(b);

    const std::string s = serializeRecord(r);
    CHECK(s.find("# BAD 3 E5 这步棋明显欠妥") != std::string::npos);

    GameRecord p;
    CHECK(parseRecord(s, &p));
    CHECK_EQ(p.blunders.size(), 1u);
    CHECK_EQ(p.blunders[0].move, 3);
    CHECK_EQ(p.blunders[0].point.x, 4);
    CHECK_EQ(p.blunders[0].point.y, 4);
    CHECK(p.blunders[0].text == "这步棋明显欠妥");
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
