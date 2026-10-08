#include "test_framework.h"

#include "ai/advisor.h"
#include "ai/conversation_http/http_conversation.h"
#include "go/game.h"

using namespace vgo;

// --- A2：多轮会话记忆（纯逻辑，无 transport） ---

TEST(memory_messages_assembly_order) {
    QJsonObject system;
    system["role"] = "system";
    system["content"] = "SYS";
    QJsonObject user;
    user["role"] = "user";
    user["content"] = "本轮问题";
    const std::vector<SessionTurn> turns = {{"第一问", "第一答"}, {"第二问", "第二答"}};

    const QJsonArray msgs = buildConversationMessages(system, QString(), turns, user);
    // [system, user(t1), assistant(a1), user(t2), assistant(a2), user]
    CHECK_EQ(msgs.size(), 6);
    CHECK(msgs.at(0).toObject()["role"].toString() == "system");
    CHECK(msgs.at(1).toObject()["role"].toString() == "user");
    CHECK(msgs.at(1).toObject()["content"].toString() == "第一问");
    CHECK(msgs.at(2).toObject()["role"].toString() == "assistant");
    CHECK(msgs.at(2).toObject()["content"].toString() == "第一答");
    CHECK(msgs.at(3).toObject()["role"].toString() == "user");
    CHECK(msgs.at(3).toObject()["content"].toString() == "第二问");
    CHECK(msgs.at(4).toObject()["role"].toString() == "assistant");
    CHECK(msgs.at(5).toObject()["role"].toString() == "user");
    CHECK(msgs.at(5).toObject()["content"].toString() == "本轮问题");
}

TEST(memory_messages_with_summary) {
    QJsonObject system;
    system["role"] = "system";
    system["content"] = "SYS";
    QJsonObject user;
    user["role"] = "user";
    user["content"] = "Q";
    const std::vector<SessionTurn> turns;

    const QJsonArray msgs =
        buildConversationMessages(system, QStringLiteral("之前聊过死活"), turns, user);
    CHECK_EQ(msgs.size(), 3);  // system + 前情提要 + user
    CHECK(msgs.at(1).toObject()["role"].toString() == "system");
    CHECK(msgs.at(1).toObject()["content"].toString().contains("前情提要"));
}

TEST(memory_no_tool_messages_in_history) {
    // SessionTurn 只有 user/assistant 两字段——工具环路的 assistant/tool 中间消息
    // 不可能进入历史。此处锁定：组装出的消息里没有任何 "tool" 角色。
    QJsonObject system;
    system["role"] = "system";
    system["content"] = "SYS";
    QJsonObject user;
    user["role"] = "user";
    user["content"] = "Q";
    const std::vector<SessionTurn> turns = {{"一问", "一答"}};
    const QJsonArray msgs = buildConversationMessages(system, QString(), turns, user);
    for (const QJsonValue& v : msgs) {
        CHECK(v.toObject()["role"].toString() != "tool");
    }
}

TEST(memory_compress_trims_and_summarizes) {
    std::vector<SessionTurn> turns = {
        {"一", "答一。"}, {"二", "答二。"}, {"三", "答三。"},
        {"四", "答四。"}, {"五", "答五。"}, {"六", "答六。"}, {"七", "答七。"},
    };
    QString summary;
    compressSession(&turns, &summary, 6);
    CHECK_EQ(turns.size(), 6u);
    CHECK(turns.front().user == "二");  // 最旧一轮被裁
    CHECK(summary.contains("问：一"));
    CHECK(summary.contains("答一"));
}

TEST(memory_compress_noop_when_under_window) {
    std::vector<SessionTurn> turns = {{"一", "答一。"}};
    QString summary;
    compressSession(&turns, &summary, 6);
    CHECK_EQ(turns.size(), 1u);
    CHECK(summary.isEmpty());
}

// --- A2 D5.2 守卫：有历史时投机缓存旁路 ---

TEST(memory_guard_bypasses_cache_with_history) {
    struct ToggleConversation : ConversationService {
        int answers = 0;
        bool history = false;
        void answer(const ConversationRequest&, Callback cb) override {
            ++answers;
            ConversationReply r;
            r.text = "带上下文的讲解";
            cb(std::move(r));
        }
        bool hasHistory() const override { return history; }
    };
    ToggleConversation conv;
    AiAdvisor a(nullptr, &conv, 5);
    a.setSpeculativeExplain(true);
    Game g(9);
    g.play(2, 2);

    // 无历史：预生成后命中缓存，追问不再调用 conversation。
    a.prewarmExplanation(g);
    CHECK_EQ(conv.answers, 1);
    std::string got;
    a.ask(g, "为什么下这里", [&](const ConversationReply& r) { got = r.text; });
    CHECK_EQ(conv.answers, 1);  // 命中缓存
    CHECK_EQ(got, std::string("带上下文的讲解"));

    // 有历史：缓存旁路，追问重新生成（避免「那白棋不应呢」串到旧答）。
    conv.history = true;
    a.ask(g, "为什么下这里", [&](const ConversationReply& r) { got = r.text; });
    CHECK_EQ(conv.answers, 2);  // 旁路缓存，重新调用
    CHECK_EQ(got, std::string("带上下文的讲解"));
}
