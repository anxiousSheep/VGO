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

TEST(advisor_emits_bad_sample) {
    // D2.5：校验拦截到「修正」时，把坏样本交给 sink（含模型原文本 + 真值摘要 + 处置）。
    struct AnalyzeEngine : EngineInterface {
        void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
            cb(EngineResult{});
        }
        void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
            Analysis a;
            a.valid = true;
            a.winrate = 0.6;
            a.score_lead = 5.5;
            cb(a);
        }
    };
    struct BadClaimConversation : ConversationService {
        void answer(const ConversationRequest&, Callback cb) override {
            ConversationReply r;
            r.text = "黑领先 99 目。"
                     "<claims>[{\"type\":\"score_lead\",\"value\":99,\"color\":\"black\"}]</claims>";
            cb(std::move(r));
        }
    };
    AnalyzeEngine eng;
    BadClaimConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    a.setTraceMeta("http", "test-model");
    std::vector<BadSample> samples;
    a.setBadSampleSink([&](const BadSample& s) { samples.push_back(s); });
    Game g(9);
    a.ask(g, "谁领先", [](const ConversationReply&) {});
    CHECK_EQ(samples.size(), 1u);
    CHECK_EQ(samples[0].disposition, std::string("corrected"));
    CHECK_EQ(samples[0].model, std::string("test-model"));
    CHECK(samples[0].reply_text.find("99") != std::string::npos);
    CHECK(samples[0].truth_summary.find("5.5") != std::string::npos);
    CHECK(!samples[0].position_hash.empty());
}

TEST(advisor_prewarm_explanation_cache_hit) {
    struct CountingConversation : ConversationService {
        int answers = 0;
        void answer(const ConversationRequest&, Callback cb) override {
            ++answers;
            ConversationReply r;
            r.text = "投机预生成的讲解";
            cb(std::move(r));
        }
    };
    ScriptedEngine eng;
    CountingConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    a.setSpeculativeExplain(true);
    Game g(9);
    g.play(2, 2);

    a.prewarmExplanation(g);
    CHECK_EQ(conv.answers, 1);  // 预生成调用一次

    // 命中缓存：追问与 explainLastMove 都不再调用 conversation。
    std::string got;
    a.ask(g, "为什么下这里", [&](const ConversationReply& r) { got = r.text; });
    CHECK_EQ(conv.answers, 1);
    CHECK_EQ(got, std::string("投机预生成的讲解"));

    std::string got2;
    a.explainLastMove(g, [&](const ConversationReply& r) { got2 = r.text; });
    CHECK_EQ(conv.answers, 1);
    CHECK_EQ(got2, std::string("投机预生成的讲解"));
}

TEST(advisor_prewarm_disabled_when_off) {
    struct CountingConversation : ConversationService {
        int answers = 0;
        void answer(const ConversationRequest&, Callback cb) override {
            ++answers;
            cb(ConversationReply{});
        }
    };
    ScriptedEngine eng;
    CountingConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    a.setSpeculativeExplain(false);  // 默认关闭
    Game g(9);
    g.play(2, 2);
    a.prewarmExplanation(g);
    CHECK_EQ(conv.answers, 0);  // 未开启，不预生成
}

// D1.8 回归：其余问题（Other）不得触发引擎预取——否则就是 MCP 模式（C++ 替模型
// 预计算）回潮。Other 应直接同步应答，工具快照来自 prewarmAnalysis 维护的 last_analysis_。
TEST(advisor_other_question_does_not_block_on_engine) {
    struct CountingEngine : EngineInterface {
        int analyze_calls = 0;
        int move_calls = 0;
        void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
            ++move_calls;
            EngineResult r;
            r.move.is_pass = true;
            cb(r);
        }
        void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
            ++analyze_calls;
            cb(Analysis{});
        }
    };
    struct CaptureConversation : ConversationService {
        bool got = false;
        void answer(const ConversationRequest&, Callback cb) override {
            got = true;
            cb(ConversationReply{});
        }
    };
    CountingEngine eng;
    CaptureConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    Game g(9);
    a.ask(g, "你好", [](const ConversationReply&) {});
    CHECK(conv.got);                  // 仍能应答
    CHECK_EQ(eng.analyze_calls, 0);   // 不再阻塞式取分析
    CHECK_EQ(eng.move_calls, 0);
}

// D1.8：prewarmAnalysis 用 Background 优先级预热 last_analysis_；随后 Other 问题的
// 请求里工具快照（facts.analysis.ownership）已带归属真值，且 ask 不再二次取分析。
TEST(advisor_prewarm_analysis_populates_tool_snapshot) {
    struct OwnershipEngine : EngineInterface {
        int analyze_calls = 0;
        void requestMove(const Board&, Color, const StrengthParams&, Callback cb) override {
            cb(EngineResult{});
        }
        void analyze(const Board&, Color, const StrengthParams&, AnalysisCallback cb) override {
            ++analyze_calls;
            Analysis a;
            a.valid = true;
            a.ownership.size = 9;
            a.ownership.value.assign(81, 0.5);
            cb(a);
        }
    };
    struct CaptureConversation : ConversationService {
        ConversationRequest captured;
        void answer(const ConversationRequest& req, Callback cb) override {
            captured = req;
            cb(ConversationReply{});
        }
    };
    OwnershipEngine eng;
    CaptureConversation conv;
    AiAdvisor a(&eng, &conv, 5);
    Game g(9);

    a.prewarmAnalysis(g);
    CHECK_EQ(eng.analyze_calls, 1);  // 预热取一次分析

    a.ask(g, "你好", [](const ConversationReply&) {});
    CHECK_EQ(eng.analyze_calls, 1);  // 提问不再取分析
    CHECK(!conv.captured.facts.analysis.ownership.empty());  // 工具快照有归属真值
}
