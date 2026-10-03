#include "test_framework.h"

#include <QFile>
#include <QTemporaryDir>

#include "ai/learner/learner_model.h"
#include "ui/model/LearnerStore.h"

using namespace vgo;

TEST(learner_store_roundtrip) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/learner.jsonl");
    LearnerStore store(path);
    store.model().recordBlunder({WeakArea::Ko, 2.0});
    store.model().recordGame(GameSummary{});
    store.save();
    CHECK(QFile::exists(path));

    LearnerStore store2(path);
    store2.load();
    CHECK_EQ(store2.model().weakScore(WeakArea::Ko), 2.0);
    CHECK_EQ(store2.model().gameCount(), 1u);
}

TEST(learner_store_enabled_flag_persists) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/learner.jsonl");
    LearnerStore store(path);
    store.setEnabled(false);
    store.save();

    LearnerStore store2(path);
    store2.load();
    CHECK(!store2.enabled());
}

TEST(learner_store_clear_removes_file) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/learner.jsonl");
    LearnerStore store(path);
    store.model().recordBlunder({WeakArea::LifeDeath, 1.0});
    store.save();
    CHECK(QFile::exists(path));

    store.clear();
    CHECK(!QFile::exists(path));
    CHECK_EQ(store.model().gameCount(), 0u);
    CHECK_EQ(store.model().weakScore(WeakArea::LifeDeath), 0.0);
}

TEST(learner_store_load_missing_is_noop) {
    QTemporaryDir dir;
    LearnerStore store(dir.path() + QStringLiteral("/nope.jsonl"));
    store.load();  // no crash, stays default
    CHECK(store.enabled());
    CHECK_EQ(store.model().gameCount(), 0u);
}

TEST(learner_store_reload_after_clear_resets) {
    // 模拟「设置里清空学习数据」后 MainWindow 重载：文件已删，load 应重置画像
    // （新手段位回到 20k），而不是残留旧数据。
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/learner.jsonl");
    LearnerStore store(path);
    store.model().recordGame(GameSummary{});
    store.save();

    LearnerStore main(path);  // 模拟 MainWindow 的持久实例
    main.load();
    CHECK_EQ(main.model().gameCount(), 1u);

    store.clear();  // 用户在设置里清空（删除文件）
    CHECK(!QFile::exists(path));

    main.load();  // 重载：应重置为空，而非残留旧数据
    CHECK_EQ(main.model().gameCount(), 0u);
    CHECK_EQ(main.model().estimateKyuRank(), 20.0);  // 新手段位回到 20 级
}

TEST(learner_store_load_rejects_garbage) {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/learner.jsonl");
    QFile f(path);
    CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("not json");
    f.close();

    LearnerStore store(path);
    store.load();  // no crash, stays default
    CHECK(store.enabled());
    CHECK_EQ(store.model().gameCount(), 0u);
}

TEST(learner_store_export_is_plaintext_profile) {
    QTemporaryDir dir;
    LearnerStore store(dir.path() + QStringLiteral("/learner.jsonl"));
    store.model().recordBlunder({WeakArea::Ko, 1.5});
    const QString out = store.exportJson();
    CHECK(out.contains(QStringLiteral("weak")));
}
