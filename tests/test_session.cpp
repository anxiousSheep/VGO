#include "test_framework.h"

#include "ai/session.h"
#include "ai/template_conversation.h"

using namespace vgo;

namespace {

struct ScriptedEngine : EngineInterface {
    std::vector<Point> moves;
    std::size_t i = 0;
    int calls = 0;
    void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
        ++calls;
        EngineResult r;
        if (i < moves.size()) r.move = {moves[i++], false};
        else r.move.is_pass = true;
        cb(r);
    }
};

}  // namespace

TEST(session_human_black_flow) {
    ScriptedEngine eng;
    eng.moves = {{1, 1}, {2, 2}};
    TemplateConversation conv;
    AIMatchController c(9, Color::Black, &eng, &conv, 5);

    CHECK(!c.isAiTurn());      // human is Black, moves first
    CHECK(c.humanMayAct());

    c.humanPlay({0, 0});
    CHECK(c.game().board().at(0, 0) == Color::Black);
    CHECK(c.humanMayAct());                        // AI replied synchronously
    CHECK(c.game().board().at(1, 1) == Color::White);

    c.humanPlay({0, 1});
    CHECK(c.game().board().at(2, 2) == Color::White);
}

TEST(session_ai_black_opens) {
    ScriptedEngine eng;
    eng.moves = {{3, 3}};
    TemplateConversation conv;
    AIMatchController c(9, Color::White, &eng, &conv, 5);

    CHECK(c.isAiTurn());
    c.start();
    CHECK(c.game().board().at(3, 3) == Color::Black);  // AI opened
    CHECK(c.humanMayAct());
    CHECK(!c.isAiTurn());
}

TEST(session_illegal_human_move_ignored) {
    ScriptedEngine eng;
    eng.moves = {{1, 1}};
    TemplateConversation conv;
    AIMatchController c(9, Color::Black, &eng, &conv, 5);

    c.humanPlay({0, 0});
    CHECK_EQ(c.game().move_number(), 2);  // human + AI
    c.humanPlay({0, 0});                  // occupied -> ignored
    CHECK_EQ(c.game().move_number(), 2);
    CHECK_EQ(eng.calls, 1);
}

TEST(session_undo_pair) {
    ScriptedEngine eng;
    eng.moves = {{1, 1}};
    TemplateConversation conv;
    AIMatchController c(9, Color::Black, &eng, &conv, 5);

    c.humanPlay({0, 0});
    CHECK_EQ(c.game().move_number(), 2);  // human + AI
    c.undo();
    CHECK_EQ(c.game().move_number(), 0);  // whole pair removed
    CHECK(c.humanMayAct());
    CHECK(c.game().board().at(0, 0) == Color::Empty);
}

TEST(session_resign) {
    ScriptedEngine eng;
    TemplateConversation conv;
    AIMatchController c(9, Color::Black, &eng, &conv, 5);

    c.resign();
    CHECK(c.resigned());
    CHECK(!c.humanMayAct());
}

TEST(session_question_reaches_conversation) {
    ScriptedEngine eng;
    TemplateConversation conv;
    AIMatchController c(9, Color::Black, &eng, &conv, 5);

    std::string got;
    c.onReply = [&](const ConversationReply& r) { got = r.text; };
    c.ask("谁领先");
    CHECK(!got.empty());
}
