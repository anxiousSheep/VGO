#include "test_framework.h"

#include "ai/conversation_http/http_conversation.h"

using namespace vgo;

// 工具调用回填：每个 tool_call 都必须有对应的 tool 消息（相同 tool_call_id、按序），
// 否则 OpenAI 兼容端点会以「assistant message with 'tool_calls' must be followed
// by tool messages…」拒绝后续请求。此回归测试锁定该不变量。
TEST(http_tool_followup_answers_every_call) {
    QJsonArray messages;
    QJsonObject user;
    user["role"] = "user";
    user["content"] = "演示一下";
    messages.append(user);

    auto makeCall = [](const char* id, const char* name) {
        QJsonObject fn;
        fn["name"] = QString::fromUtf8(name);
        fn["arguments"] = QStringLiteral("{}");
        QJsonObject call;
        call["id"] = QString::fromUtf8(id);
        call["type"] = "function";
        call["function"] = fn;
        return call;
    };
    QJsonArray toolCalls;
    toolCalls.append(makeCall("call_a", "demonstrate_go_moves"));
    toolCalls.append(makeCall("call_b", "count_territory"));

    int execCount = 0;
    const QJsonArray next = buildToolFollowup(
        messages, QString(), toolCalls,
        [&](const QJsonObject&) { ++execCount; return std::string("ok"); });

    CHECK_EQ(execCount, 2);
    // messages: [user, assistant(tool_calls), tool(call_a), tool(call_b)]
    CHECK_EQ(next.size(), 4);
    const QJsonObject assistant = next.at(1).toObject();
    CHECK(assistant["role"].toString() == "assistant");
    CHECK_EQ(assistant["tool_calls"].toArray().size(), 2);
    const QJsonObject t0 = next.at(2).toObject();
    CHECK(t0["role"].toString() == "tool");
    CHECK(t0["tool_call_id"].toString() == "call_a");
    CHECK(t0["content"].toString() == "ok");
    const QJsonObject t1 = next.at(3).toObject();
    CHECK(t1["role"].toString() == "tool");
    CHECK(t1["tool_call_id"].toString() == "call_b");
    CHECK(t1["content"].toString() == "ok");
}

TEST(http_tool_followup_single_call) {
    QJsonArray messages;
    QJsonArray toolCalls;
    QJsonObject fn;
    fn["name"] = "demonstrate_go_moves";
    fn["arguments"] = "{}";
    QJsonObject call;
    call["id"] = "call_1";
    call["type"] = "function";
    call["function"] = fn;
    toolCalls.append(call);

    const QJsonArray next = buildToolFollowup(
        messages, QString(), toolCalls,
        [&](const QJsonObject&) { return std::string("done"); });
    CHECK_EQ(next.size(), 2);  // assistant + 一个 tool 消息
    CHECK(next.at(0).toObject()["role"].toString() == "assistant");
    CHECK(next.at(1).toObject()["role"].toString() == "tool");
    CHECK(next.at(1).toObject()["tool_call_id"].toString() == "call_1");
}

TEST(http_tool_followup_preserves_assistant_content) {
    QJsonArray messages;
    QJsonArray toolCalls;
    QJsonObject fn;
    fn["name"] = "n";
    fn["arguments"] = "{}";
    QJsonObject call;
    call["id"] = "call_1";
    call["type"] = "function";
    call["function"] = fn;
    toolCalls.append(call);

    const QJsonArray next = buildToolFollowup(
        messages, QStringLiteral("我先演示一下"), toolCalls,
        [&](const QJsonObject&) { return std::string("r"); });
    CHECK(next.at(0).toObject()["content"].toString() == "我先演示一下");
}

// D1.9：Other 问题（未命中专用路径）的 user prompt 不再预灌引擎事实（胜率 / 候选点 /
// 死棋清单），改由模型走工具按需取；只保留无引擎基础事实（棋盘 / 规则计分 / 问题）。
TEST(http_user_prompt_other_strips_engine_facts) {
    ConversationRequest req;
    req.question = "你好";  // Other
    req.size = 9;
    req.to_move = Color::Black;
    req.score_black = 3.5;
    req.move_number = 10;
    req.board_context = "X O .";
    req.facts.analysis.valid = true;
    req.facts.analysis.winrate = 0.6;
    req.facts.analysis.score_lead = 5.0;
    MoveCandidate c;
    c.p = {3, 3};
    c.winrate = 0.6;
    c.score_lead = 5.0;
    req.facts.analysis.candidates.push_back(c);

    const QString s = buildUserPrompt(req, QuestionClass::Other);
    CHECK(!s.contains(QStringLiteral("胜率")));     // 无引擎胜率
    CHECK(!s.contains(QStringLiteral("候选点")));   // 无候选点
    CHECK(s.contains(QStringLiteral("规则计分")));  // 保留规则计分基础事实
    CHECK(s.contains(QStringLiteral("棋盘")));
    CHECK(s.contains(QStringLiteral("你好")));
}

// D1.9：专用路径（谁领先）仍注入引擎事实（胜率），供模型直接作答。
TEST(http_user_prompt_winning_keeps_engine_facts) {
    ConversationRequest req;
    req.question = "谁领先";
    req.size = 9;
    req.to_move = Color::Black;
    req.score_black = 3.5;
    req.move_number = 10;
    req.board_context = "X O .";
    req.facts.analysis.valid = true;
    req.facts.analysis.winrate = 0.6;
    req.facts.analysis.score_lead = 5.0;

    const QString s = buildUserPrompt(req, QuestionClass::WhoIsWinning);
    CHECK(s.contains(QStringLiteral("胜率")));
    CHECK(!s.contains(QStringLiteral("规则计分")));  // 有真值时走 KataGo 行
}

// L16：HttpConversationService 构造时改为直连（NoProxy，绕过系统代理检测的网卡枚举），
// 并据此判定 available。冒烟测试：构造不崩溃，且配置完整时 available=true、缺配置时 false。
TEST(http_service_constructs_available_and_direct) {
    HttpConversationService svc{QStringLiteral("http://localhost:11434/v1"), QString(),
                                QStringLiteral("qwen2.5:7b")};
    CHECK(svc.available());
    HttpConversationService unconfigured{QString(), QString(), QString()};
    CHECK(!unconfigured.available());
}

// D5.3 模型路由：无轻模型时单模型（强模型），声明工具直到 depth 上限；有轻模型且有
// 工具时，depth 0 用轻模型挑工具、depth>=1 用强模型做最终讲解（不再声明工具）。
TEST(http_route_turn_single_model) {
    const QString strong = QStringLiteral("deepseek-chat");
    // 无 router → 单模型。
    TurnRoute r = routeTurn(strong, QString(), 0, true, true);
    CHECK(r.model == strong);
    CHECK(r.declare_tools);
    // depth 达上限后不再声明工具（D1.10 有界环路）。
    r = routeTurn(strong, QString(), 4, true, true);
    CHECK(!r.declare_tools);
    // 超时后不再声明工具。
    r = routeTurn(strong, QString(), 1, true, false);
    CHECK(!r.declare_tools);
}

TEST(http_route_turn_light_then_strong) {
    const QString strong = QStringLiteral("deepseek-chat");
    const QString router = QStringLiteral("qwen2.5:7b");
    // depth 0：轻模型挑工具。
    TurnRoute r = routeTurn(strong, router, 0, true, true);
    CHECK(r.model == router);
    CHECK(r.declare_tools);
    // depth >= 1：强模型最终讲解，不再声明工具。
    r = routeTurn(strong, router, 1, true, true);
    CHECK(r.model == strong);
    CHECK(!r.declare_tools);
    // 无工具可调时，即便配了轻模型也退化为单模型。
    r = routeTurn(strong, router, 0, false, true);
    CHECK(r.model == strong);
    CHECK(!r.declare_tools);
}

TEST(http_service_router_model_settable) {
    HttpConversationService svc{QStringLiteral("http://localhost:11434/v1"), QString(),
                                QStringLiteral("deepseek-chat")};
    CHECK(svc.routerModel().isEmpty());
    svc.setRouterModel(QStringLiteral("qwen2.5:7b"));
    CHECK(svc.routerModel() == QStringLiteral("qwen2.5:7b"));
}
