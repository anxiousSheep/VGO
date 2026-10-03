#include "test_framework.h"

#include <QFile>
#include <QTemporaryDir>

#include "ai/agent/trace.h"
#include "ui/model/TraceStore.h"

using namespace vgo;

namespace {

AgentTraceRecord makeRecord(const char* hash, const char* route, int latency, const char* verify) {
    AgentTraceRecord r;
    r.position_hash = hash;
    r.route = route;
    r.model = "test";
    r.latency_ms = latency;
    r.verify = verify;
    return r;
}

}  // namespace

TEST(trace_store_append_and_load_recent) {
    QTemporaryDir dir;
    TraceStore store(dir.path() + QStringLiteral("/agent_trace.jsonl"));
    store.append(makeRecord("aaaaaaaaaaaaaaaa", "http", 10, "pass"));
    store.append(makeRecord("bbbbbbbbbbbbbbbb", "template", 20, "none"));
    store.append(makeRecord("cccccccccccccccc", "http", 30, "corrected"));

    const auto recs = store.loadRecent(2);
    CHECK_EQ(recs.size(), 2u);
    CHECK_EQ(recs[0].position_hash, std::string("bbbbbbbbbbbbbbbb"));
    CHECK_EQ(recs[1].position_hash, std::string("cccccccccccccccc"));
    CHECK_EQ(recs[1].verify, std::string("corrected"));
}

TEST(trace_store_load_missing_is_empty) {
    QTemporaryDir dir;
    TraceStore store(dir.path() + QStringLiteral("/nope.jsonl"));
    const auto recs = store.loadRecent(100);
    CHECK_EQ(recs.size(), 0u);
}

TEST(trace_store_clear_removes_file) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/agent_trace.jsonl");
    TraceStore store(path);
    store.append(makeRecord("dddddddddddddddd", "http", 5, "pass"));
    CHECK(QFile::exists(path));

    store.clear();
    CHECK(!QFile::exists(path));
    CHECK_EQ(store.loadRecent(100).size(), 0u);
}

TEST(trace_store_skips_garbage_lines) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/agent_trace.jsonl");
    TraceStore store(path);
    store.append(makeRecord("eeeeeeeeeeeeeeee", "http", 1, "pass"));
    // 手工追加一行坏数据，验证 loadRecent 跳过而不崩。
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly | QIODevice::Append));
    f.write("this is not json\n");
    f.close();

    const auto recs = store.loadRecent(100);
    CHECK_EQ(recs.size(), 1u);
    CHECK_EQ(recs[0].position_hash, std::string("eeeeeeeeeeeeeeee"));
}
