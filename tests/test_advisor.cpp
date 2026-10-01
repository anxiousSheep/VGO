#include "test_framework.h"

#include "ai/advisor.h"
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

TEST(advisor_who_is_winning) {
    ScriptedEngine eng;
    TemplateConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    Game g(9);

    std::string got;
    a.ask(g, "谁领先", [&](const ConversationReply& r) { got = r.text; });
    CHECK(!got.empty());
}

TEST(advisor_where_to_play_highlights) {
    ScriptedEngine eng;
    eng.moves = {{3, 3}};
    TemplateConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    Game g(9);

    std::vector<Point> highlight;
    a.ask(g, "我该下哪", [&](const ConversationReply& r) { highlight = r.highlight; });
    CHECK(!highlight.empty());
    CHECK_EQ(highlight[0].x, 3);
    CHECK_EQ(highlight[0].y, 3);
    CHECK_EQ(eng.calls, 1);
}

TEST(advisor_explain_last_move) {
    ScriptedEngine eng;
    TemplateConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    Game g(9);
    g.play(2, 2);

    std::string got;
    a.explainLastMove(g, [&](const ConversationReply& r) { got = r.text; });
    CHECK(!got.empty());
}

TEST(advisor_cancel_is_safe) {
    ScriptedEngine eng;
    TemplateConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    a.cancel();
    Game g(9);
    a.ask(g, "谁领先", [](const ConversationReply&) {});
}
