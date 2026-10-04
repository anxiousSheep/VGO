#include "test_framework.h"

#include <cmath>
#include <set>

#include "ai/advisor.h"
#include "ai/record.h"
#include "ai/session.h"
#include "ai/sgf.h"
#include "ai/template_conversation.h"
#include "go/handicap.h"

using namespace vgo;

namespace {

// Parenthesized point helper: braces ({x,y}) would be split by the comma inside
// the CHECK() macro's argument list, so construct points via a function call.
Point pt(int x, int y) { return Point{x, y}; }

struct ScriptedEngine : EngineInterface {
    std::vector<Point> moves;
    std::size_t i = 0;
    void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
        EngineResult r;
        if (i < moves.size()) r.move = {moves[i++], false};
        else r.move.is_pass = true;
        cb(r);
    }
};

}  // namespace

TEST(handicap_points_counts) {
    CHECK(handicapPoints(9, 0).empty());
    CHECK(handicapPoints(9, 1).empty());   // even game
    for (int count = 2; count <= 9; ++count) {
        const auto pts = handicapPoints(9, count);
        CHECK_EQ(pts.size(), static_cast<std::size_t>(count));
    }
    // Counts above 9 clamp to 9.
    CHECK_EQ(handicapPoints(9, 12).size(), 9u);
    // Nonsense sizes produce no stones.
    CHECK(handicapPoints(5, 2).empty());
}

TEST(handicap_points_standard) {
    // 9x9: 3-3 corner points (offset 2), center at (4,4).
    const auto two9 = handicapPoints(9, 2);
    CHECK_EQ(two9.size(), 2u);
    CHECK(two9[0] == pt(6, 2));  // 右上
    CHECK(two9[1] == pt(2, 6));  // 左下

    const auto nine9 = handicapPoints(9, 9);
    CHECK_EQ(nine9.size(), 9u);
    CHECK(nine9[4] == pt(4, 4));  // 天元

    // 19x19: 4-4 corner points (offset 3).
    const auto four19 = handicapPoints(19, 4);
    CHECK(four19[0] == pt(15, 3));   // 右上
    CHECK(four19[1] == pt(3, 15));   // 左下
    CHECK(four19[2] == pt(3, 3));    // 左上
    CHECK(four19[3] == pt(15, 15));  // 右下
}

TEST(handicap_points_in_bounds_and_distinct) {
    for (int size : {9, 13, 19}) {
        for (int count = 2; count <= 9; ++count) {
            const auto pts = handicapPoints(size, count);
            std::set<std::pair<int, int>> seen;
            for (const Point& p : pts) {
                CHECK(p.x >= 0 && p.x < size && p.y >= 0 && p.y < size);
                const bool fresh = seen.insert(std::make_pair(p.x, p.y)).second;
                CHECK(fresh);  // no overlap
            }
        }
    }
}

TEST(game_handicap_constructor) {
    const auto h = handicapPoints(9, 3);
    Game g(9, 0.5, h);

    CHECK(std::abs(g.komi() - 0.5) < 1e-9);
    CHECK(g.current() == Color::White);       // White opens after handicap
    CHECK_EQ(g.setup_stones().size(), 3u);
    for (const Point& p : h) {
        CHECK(g.board().at(p.x, p.y) == Color::Black);
    }
    CHECK_EQ(g.move_number(), 0);             // setup stones are not moves
    CHECK(g.moves().empty());                 // not part of the move sequence

    // An empty handicap is an even game (Black to move, no setup stones).
    Game even(9, 7.5, {});
    CHECK(even.current() == Color::Black);
    CHECK(even.setup_stones().empty());
}

TEST(game_handicap_record_roundtrip_vgo) {
    const auto h = handicapPoints(9, 2);
    Game g(9, 0.5, h);
    g.play(0, 0);  // White's first move (White opens)
    g.play(1, 1);  // Black reply

    GameRecord r = recordFromGame(g);
    CHECK_EQ(r.setup_black.size(), 2u);
    CHECK(r.komi == 0.5);

    const std::string s = serializeRecord(r);
    GameRecord p;
    CHECK(parseRecord(s, &p));
    CHECK_EQ(p.setup_black.size(), 2u);
    CHECK(p.setup_black == h);
    CHECK_EQ(p.moves.size(), 2u);

    // Rebuild: setup stones on the board, White to move, then replay moves.
    Game g2 = recordToGame(p, 2);
    for (const Point& pt : h) CHECK(g2.board().at(pt.x, pt.y) == Color::Black);
    CHECK(g2.board().at(0, 0) == Color::White);
    CHECK(g2.board().at(1, 1) == Color::Black);
}

TEST(game_handicap_record_roundtrip_sgf) {
    GameRecord r;
    r.size = 9;
    r.komi = 0.5;
    r.setup_black = handicapPoints(9, 2);
    r.moves.push_back(MoveRecord{Color::White, Move{{2, 2}, false}});

    const std::string s = serializeSgf(r);
    CHECK(s.find("AB[") != std::string::npos);  // 让子摆子以 AB 属性输出

    GameRecord p;
    CHECK(parseSgf(s, &p));
    CHECK_EQ(p.setup_black.size(), 2u);
    CHECK(p.setup_black == r.setup_black);
    CHECK_EQ(p.moves.size(), 1u);

    Game g = recordToGame(p, 1);
    CHECK(g.current() == Color::Black);  // after White's opening move
    for (const Point& pt : r.setup_black) CHECK(g.board().at(pt.x, pt.y) == Color::Black);
}

TEST(session_handicap_ai_opens) {
    ScriptedEngine eng;
    eng.moves = {{1, 1}};  // White's opening move
    TemplateConversation conv;
    AiAdvisor advisor(&eng, &conv, 5);
    const auto h = handicapPoints(9, 2);
    AIMatchController c(9, Color::Black, &eng, &advisor, 5, 20.0, h);

    CHECK(c.isAiTurn());       // handicap: AI (White) opens
    CHECK(!c.humanMayAct());

    // Handicap stones present as Black.
    for (const Point& p : h) CHECK(c.game().board().at(p.x, p.y) == Color::Black);
    CHECK(c.game().current() == Color::White);

    c.start();
    CHECK(c.game().board().at(1, 1) == Color::White);  // AI played White's move
    CHECK(!c.isAiTurn());
    CHECK(c.humanMayAct());
}
