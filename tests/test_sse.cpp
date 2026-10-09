#include "test_framework.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ai/conversation_http/http_conversation.h"
#include "ai/conversation_http/sse_parser.h"

using namespace vgo;

namespace {

// 造一条只含 content 增量的 SSE data 事件。
QJsonObject contentDeltaObj(const std::string& content) {
    QJsonObject delta;
    delta["content"] = QString::fromStdString(content);
    QJsonObject choice;
    choice["delta"] = delta;
    choice["index"] = 0;
    QJsonObject obj;
    obj["choices"] = QJsonArray{choice};
    return obj;
}

std::string sseLine(const QJsonObject& o) {
    return "data: " + QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString() + "\n\n";
}

// 把 delta 包进 choices[0].delta 的完整事件，再造 SSE data 行。
std::string deltaSseLine(const QJsonObject& delta) {
    QJsonObject choice;
    choice["delta"] = delta;
    choice["index"] = 0;
    QJsonObject obj;
    obj["choices"] = QJsonArray{choice};
    return sseLine(obj);
}

}  // namespace

// --- B3：SseParser（纯逻辑） ---

TEST(sse_parse_content_deltas) {
    SseParser p;
    p.feed(sseLine(contentDeltaObj("你")));
    p.feed(sseLine(contentDeltaObj("好")));
    const auto d = p.drainContentDeltas();
    CHECK_EQ(d.size(), std::size_t(2));
    CHECK_EQ(d[0], std::string("你"));
    CHECK_EQ(d[1], std::string("好"));
    CHECK(!p.done());
}

TEST(sse_parse_cross_chunk_half_line) {
    // 跨块半行：一条 data: 行被拆成两个 feed。
    SseParser p;
    const std::string full = sseLine(contentDeltaObj("拆开"));
    p.feed(full.substr(0, full.size() / 2));
    p.feed(full.substr(full.size() / 2));
    const auto d = p.drainContentDeltas();
    CHECK_EQ(d.size(), std::size_t(1));
    CHECK_EQ(d[0], std::string("拆开"));
}

TEST(sse_parse_done_marker) {
    SseParser p;
    p.feed("data: [DONE]\n\n");
    CHECK(p.done());
}

TEST(sse_parse_tool_call_deltas) {
    SseParser p;
    {
        QJsonObject fn;
        fn["name"] = "count_territory";
        fn["arguments"] = "";
        QJsonObject tc;
        tc["index"] = 0;
        tc["id"] = "call_1";
        tc["type"] = "function";
        tc["function"] = fn;
        QJsonObject delta;
        delta["tool_calls"] = QJsonArray{tc};
        p.feed(deltaSseLine(delta));
    }
    {
        QJsonObject fn;
        fn["arguments"] = "{\"a\":1}";
        QJsonObject tc;
        tc["index"] = 0;
        tc["function"] = fn;
        QJsonObject delta;
        delta["tool_calls"] = QJsonArray{tc};
        p.feed(deltaSseLine(delta));
    }
    const auto tools = p.drainToolCallDeltas();
    CHECK_EQ(tools.size(), std::size_t(2));
    CHECK(tools[0]["id"].toString() == "call_1");
    CHECK(tools[0]["function"].toObject()["name"].toString() == "count_territory");
    CHECK(tools[1]["function"].toObject()["arguments"].toString() == "{\"a\":1}");
}

TEST(sse_parse_malformed_json_tolerated) {
    SseParser p;
    p.feed("data: not-json\n\n");
    p.feed(sseLine(contentDeltaObj("ok")));
    const auto d = p.drainContentDeltas();
    CHECK_EQ(d.size(), std::size_t(1));
    CHECK_EQ(d[0], std::string("ok"));
}

TEST(sse_parse_ignores_non_data_lines) {
    SseParser p;
    p.feed("event: message\n");
    p.feed("id: 1\n");
    p.feed(sseLine(contentDeltaObj("好")));
    const auto d = p.drainContentDeltas();
    CHECK_EQ(d.size(), std::size_t(1));
    CHECK_EQ(d[0], std::string("好"));
}

// --- B3：tool_calls 增量聚合 ---

TEST(sse_build_tool_calls_from_deltas) {
    std::vector<QJsonObject> deltas;
    {
        QJsonObject fn;
        fn["name"] = "count_territory";
        fn["arguments"] = "{\"side\":";
        QJsonObject d;
        d["index"] = 0;
        d["id"] = "call_x";
        d["type"] = "function";
        d["function"] = fn;
        deltas.push_back(d);
    }
    {
        QJsonObject fn;
        fn["arguments"] = "\"black\"}";
        QJsonObject d;
        d["index"] = 0;
        d["function"] = fn;
        deltas.push_back(d);
    }

    const QJsonArray calls = buildToolCallsFromDeltas(deltas);
    CHECK_EQ(calls.size(), 1);
    const QJsonObject c = calls.first().toObject();
    CHECK(c["id"].toString() == "call_x");
    CHECK(c["function"].toObject()["name"].toString() == "count_territory");
    CHECK(c["function"].toObject()["arguments"].toString() == "{\"side\":\"black\"}");
}
