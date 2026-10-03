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

TEST(advisor_injects_demo_context) {
    struct CaptureConversation : ConversationService {
        ConversationRequest captured;
        bool got = false;
        void answer(const ConversationRequest& req, Callback cb) override {
            captured = req;
            got = true;
            cb(ConversationReply{});
        }
    };
    ScriptedEngine eng;
    CaptureConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    a.setDemoState(true, "白 H6、黑 G5");
    Game g(9);
    a.ask(g, "继续", [](const ConversationReply&) {});
    CHECK(conv.got);
    CHECK(conv.captured.in_demo);
    CHECK_EQ(conv.captured.demo_moves, std::string("白 H6、黑 G5"));

    // 退出试下后不再注入。
    a.setDemoState(false, "");
    a.ask(g, "继续", [](const ConversationReply&) {});
    CHECK(!conv.captured.in_demo);
}

TEST(advisor_mounts_verdicts_and_pv) {
    // 引擎给出有效分析（含主变化），对话服务回一段带 <claims> 的文本：校验闸门应把
    // 断言挂到 reply.verdicts、把主变化挂到 reply.pv（D2.4）。
    struct AnalyzeEngine : EngineInterface {
        void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
            cb(EngineResult{});
        }
        void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
            Analysis a;
            a.valid = true;
            a.winrate = 0.6;
            a.score_lead = 5.5;
            a.best.p = {3, 3};
            a.best.pv = {{3, 3}, {4, 4}, {5, 5}};
            a.candidates = {a.best};
            cb(a);
        }
    };
    struct ClaimsConversation : ConversationService {
        void answer(const ConversationRequest&, Callback cb) override {
            ConversationReply r;
            r.text = "黑领先 5.5 目。"
                     "<claims>[{\"type\":\"score_lead\",\"value\":5.5,\"color\":\"black\"}]</claims>";
            cb(std::move(r));
        }
    };
    AnalyzeEngine eng;
    ClaimsConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    Game g(9);

    std::vector<Point> pv;
    std::vector<ClaimVerdict> verdicts;
    a.ask(g, "谁领先", [&](const ConversationReply& r) {
        pv = r.pv;
        verdicts = r.verdicts;
    });
    CHECK_EQ(pv.size(), 3u);
    CHECK_EQ(pv[0].x, 3);
    CHECK_EQ(verdicts.size(), 1u);
    CHECK(verdicts[0].claim.type == ClaimType::ScoreLead);
    CHECK_EQ(verdicts[0].claim.source, std::string("KataGo 分析"));
    CHECK(verdicts[0].disposition == ClaimDisposition::Pass);
}

TEST(advisor_emits_trace) {
    ScriptedEngine eng;
    TemplateConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    a.setTraceMeta("template", "test-model");
    std::vector<AgentTraceRecord> recs;
    a.setTraceSink([&](const AgentTraceRecord& r) { recs.push_back(r); });
    Game g(9);
    g.play(2, 2);
    a.explainLastMove(g, [](const ConversationReply&) {});
    CHECK_EQ(recs.size(), 1u);
    CHECK_EQ(recs[0].route, std::string("template"));
    CHECK_EQ(recs[0].model, std::string("test-model"));
    CHECK_EQ(recs[0].verify, std::string("none"));  // 模板解说无引擎真值
    CHECK(!recs[0].position_hash.empty());
    CHECK(recs[0].latency_ms >= 0);
}
