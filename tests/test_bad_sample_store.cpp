#include "test_framework.h"

#include <QFile>
#include <QTemporaryDir>

#include "ai/verify/bad_sample.h"
#include "ui/model/BadSampleStore.h"

using namespace vgo;

namespace {

BadSample makeSample(const char* q, const char* disposition) {
    BadSample s;
    s.ts_ms = 1;
    s.position_hash = "abcd";
    s.question = q;
    s.reply_text = "黑领先 99 目";
    s.disposition = disposition;
    s.truth_summary = "黑领先 5.5 目";
    s.model = "test";
    return s;
}

}  // namespace

TEST(bad_sample_store_append_and_load) {
    QTemporaryDir dir;
    BadSampleStore store(dir.path() + QStringLiteral("/bad_samples.jsonl"));
    store.append(makeSample("谁领先", "corrected"));
    store.append(makeSample("谁领先", "unverifiable"));

    const auto recs = store.loadRecent(2);
    CHECK_EQ(recs.size(), 2u);
    CHECK_EQ(recs[0].disposition, std::string("corrected"));
    CHECK_EQ(recs[1].disposition, std::string("unverifiable"));
}

TEST(bad_sample_store_load_missing_is_empty) {
    QTemporaryDir dir;
    BadSampleStore store(dir.path() + QStringLiteral("/nope.jsonl"));
    CHECK_EQ(store.loadRecent(100).size(), 0u);
}

TEST(bad_sample_store_clear_removes_file) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/bad_samples.jsonl");
    BadSampleStore store(path);
    store.append(makeSample("谁领先", "corrected"));
    CHECK(QFile::exists(path));
    store.clear();
    CHECK(!QFile::exists(path));
    CHECK_EQ(store.loadRecent(100).size(), 0u);
}

TEST(bad_sample_store_skips_garbage_lines) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/bad_samples.jsonl");
    BadSampleStore store(path);
    store.append(makeSample("谁领先", "corrected"));
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly | QIODevice::Append));
    f.write("garbage not json\n");
    f.close();
    CHECK_EQ(store.loadRecent(100).size(), 1u);
}
