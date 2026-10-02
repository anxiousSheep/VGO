#include "test_framework.h"

#include "ai/template_conversation.h"

using namespace vgo;

namespace {

ConversationReply ask(TemplateConversation& tc, const std::string& q,
                      double score = 1.5, const GroundedFacts& facts = {}) {
    ConversationReply out;
    ConversationRequest req;
    req.question = q;
    req.score_black = score;
    req.move_number = 10;
    req.size = 9;
    req.to_move = Color::Black;
    req.last_mover = Color::White;
    req.facts = facts;
    req.profile = PlayerProfile{5, true};
    tc.answer(req, [&](ConversationReply r) { out = std::move(r); });
    return out;
}

}  // namespace

TEST(template_classify) {
    CHECK(classifyQuestion("谁领先") == QuestionClass::WhoIsWinning);
    CHECK(classifyQuestion("我该下哪") == QuestionClass::WhereToPlay);
    CHECK(classifyQuestion("你为什么下这里") == QuestionClass::WhyThisMove);
    CHECK(classifyQuestion("今天天气不错") == QuestionClass::Other);
}

TEST(template_who_is_winning) {
    TemplateConversation tc;
    auto r = ask(tc, "谁领先");
    CHECK(!r.text.empty());
    CHECK(!r.unsure);
}

TEST(template_where_no_engine) {
    TemplateConversation tc;
    auto r = ask(tc, "我该下哪");
    CHECK(r.unsure);  // no analysis -> honest "no engine"
    CHECK(!r.text.empty());
}

TEST(template_where_with_engine) {
    TemplateConversation tc;
    GroundedFacts f;
    f.analysis.valid = true;
    MoveCandidate c;
    c.p = {3, 5};
    f.analysis.candidates.push_back(c);
    auto r = ask(tc, "我该下哪", 1.5, f);
    CHECK(!r.text.empty());
    CHECK(!r.unsure);
    CHECK_EQ(r.highlight.size(), std::size_t(1));
    CHECK_EQ(r.highlight[0].x, 3);
}

TEST(template_why) {
    TemplateConversation tc;
    GroundedFacts f;
    StructuralFact c;
    c.type = FactType::Captures;
    c.stones = {{0, 0}, {0, 1}};
    f.facts.push_back(c);
    auto r = ask(tc, "为什么下这里", 1.5, f);
    CHECK(!r.text.empty());
    CHECK(!r.unsure);
    CHECK(contains(r.text, "提掉"));
}

TEST(template_other) {
    TemplateConversation tc;
    auto r = ask(tc, "你好");
    CHECK(r.unsure);
    CHECK(!r.text.empty());
}

TEST(template_classify_new_classes) {
    CHECK(classifyQuestion("为什么下这里不好") == QuestionClass::MoveQuality);
    CHECK(classifyQuestion("这步行吗") == QuestionClass::MoveQuality);
    CHECK(classifyQuestion("下 G3 怎么样") == QuestionClass::MoveQuality);
    CHECK(classifyQuestion("为什么做不活") == QuestionClass::LifeDeath);
    CHECK(classifyQuestion("这块棋死没死") == QuestionClass::LifeDeath);
    // 「为什么下这里」无「不好」，仍是 WhyThisMove。
    CHECK(classifyQuestion("为什么下这里") == QuestionClass::WhyThisMove);
}

TEST(template_move_quality_with_eval) {
    TemplateConversation tc;
    GroundedFacts f;
    MoveEvaluation e;
    e.p = {3, 3};
    e.legal = true;
    e.valid = true;
    e.winrate_before = 0.55;
    e.winrate_after = 0.40;
    e.lead_before = 2.0;
    e.lead_after = -1.0;
    f.move_eval = e;
    auto r = ask(tc, "为什么这步不好", 1.5, f);
    CHECK(!r.unsure);
    CHECK(!r.text.empty());
}

TEST(template_move_quality_no_eval) {
    TemplateConversation tc;
    auto r = ask(tc, "这步怎么样");
    CHECK(r.unsure);  // 无 KataGo，无法量化
}

TEST(template_life_death_with_result) {
    TemplateConversation tc;
    GroundedFacts f;
    LifeDeathResult lr;
    lr.alive = false;
    lr.group = {{0, 0}, {1, 0}};
    lr.saving_moves = {};
    lr.reason = "无法做出两只眼";
    f.life_death = lr;
    auto r = ask(tc, "为什么做不活", 1.5, f);
    CHECK(!r.unsure);
    CHECK(!r.text.empty());
}
