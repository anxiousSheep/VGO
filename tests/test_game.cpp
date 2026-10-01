#include "test_framework.h"

#include "game.h"

using namespace vgo;

TEST(game_initial_state) {
    Game g(9);
    CHECK(g.current() == Color::Black);
    CHECK_EQ(g.move_number(), 0);
    CHECK(!g.is_over());
}

TEST(game_legal_and_play) {
    Game g(9);
    CHECK(g.is_legal(0, 0));
    CHECK(g.play(0, 0));
    CHECK_EQ(g.move_number(), 1);
    CHECK(g.current() == Color::White);
    CHECK(g.board().at(0, 0) == Color::Black);
}

TEST(game_illegal_occupied) {
    Game g(9);
    CHECK(g.play(2, 2));
    CHECK(!g.is_legal(2, 2));
    CHECK(!g.play(2, 2));
    CHECK_EQ(g.move_number(), 1);
}

TEST(game_illegal_out_of_bounds) {
    Game g(9);
    CHECK(!g.is_legal(-1, 0));
    CHECK(!g.is_legal(0, 9));
    CHECK(!g.is_legal(9, 0));
}

TEST(game_suicide_illegal) {
    Board b(5);
    b.set(0, 1, Color::White);
    b.set(1, 0, Color::White);
    Game g(b, Color::Black);
    CHECK(!g.is_legal(0, 0));
}

TEST(game_capture) {
    Board b(5);
    b.set(1, 1, Color::White);
    b.set(0, 1, Color::Black);
    b.set(2, 1, Color::Black);
    b.set(1, 0, Color::Black);
    Game g(b, Color::Black);
    CHECK(g.play(1, 2));
    CHECK(g.board().at(1, 1) == Color::Empty);
    CHECK_EQ(g.captures(Color::Black), 1);
}

TEST(game_ko_superko) {
    // Minimal edge ko: Black captures a single stone, White's recapture would
    // recreate the previous position and must be forbidden by superko.
    Board b(5);
    b.set(2, 2, Color::Black);
    b.set(1, 3, Color::Black);
    b.set(3, 3, Color::Black);
    b.set(2, 3, Color::White);
    b.set(1, 4, Color::White);
    b.set(3, 4, Color::White);
    Game g(b, Color::Black);
    CHECK(g.is_legal(2, 4));
    CHECK(g.play(2, 4));
    CHECK(g.board().at(2, 3) == Color::Empty);
    CHECK(g.current() == Color::White);
    CHECK(!g.is_legal(2, 3));  // recapture forbidden
}

TEST(game_two_pass_ends) {
    Game g(9);
    g.pass();
    CHECK(!g.is_over());
    g.pass();
    CHECK(g.is_over());
}

TEST(game_settle_ends_without_move) {
    Game g(9);
    g.play(2, 2);
    CHECK(!g.is_over());
    g.settle();
    CHECK(g.is_over());
    CHECK_EQ(g.move_number(), 1);  // 协商终局不新增手数
}

TEST(game_undo) {
    Game g(5);
    g.play(2, 2);  // Black
    g.play(2, 3);  // White
    CHECK_EQ(g.move_number(), 2);
    CHECK(g.current() == Color::Black);

    CHECK(g.undo());
    CHECK_EQ(g.move_number(), 1);
    CHECK(g.current() == Color::White);
    CHECK(g.board().at(2, 3) == Color::Empty);
    CHECK(g.board().at(2, 2) == Color::Black);

    CHECK(g.undo());
    CHECK_EQ(g.move_number(), 0);
    CHECK(g.current() == Color::Black);
    CHECK(g.board().at(2, 2) == Color::Empty);

    CHECK(!g.undo());  // nothing left to undo
}

TEST(game_score_enclosed_territory) {
    Board b(3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            if (!(x == 1 && y == 1)) b.set(x, y, Color::Black);
        }
    }
    Game g(b, Color::White);
    const auto [black, white] = g.score_area();
    CHECK_EQ(black, 9);  // 8 stones + 1 enclosed point
    CHECK_EQ(white, 0);
    CHECK(g.winner() == Color::Black);  // 9 - 0 - 7.5 = 1.5
}

TEST(game_random_play_terminates) {
    unsigned seed = 12345u;
    Game g(5);
    int guard = 0;
    while (!g.is_over() && guard < 2000) {
        const auto moves = g.legal_moves();
        if (moves.empty()) {
            g.pass();
        } else {
            seed = seed * 1103515245u + 12345u;
            const Point m = moves[seed % moves.size()];
            CHECK(g.play(m.x, m.y));
        }
        ++guard;
    }
    CHECK(g.is_over());
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (g.board().at(x, y) != Color::Empty) {
                CHECK(!g.board().liberties(x, y).empty());
            }
        }
    }
}
