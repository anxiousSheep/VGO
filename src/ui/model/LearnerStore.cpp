#include "LearnerStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

#include "ai/json.h"
#include "ModelManager.h"

namespace vgo {

QString LearnerStore::defaultPath() {
    return ModelManager::instance().dataDir() + QStringLiteral("/learner.jsonl");
}

LearnerStore::LearnerStore(QString path) : path_(std::move(path)) {}

void LearnerStore::load() {
    QFile f(path_);
    // 文件缺失 / 为空 / 损坏（含「清空学习数据」后）时重置为默认画像：新手 20k、
    // 开关开启，避免残留旧数据（如新手段位未随清空回到 20 级）。
    const auto reset = [this] {
        model_.clear();
        enabled_ = true;
    };
    if (!f.open(QIODevice::ReadOnly)) { reset(); return; }
    const QByteArray bytes = f.readAll().trimmed();
    if (bytes.isEmpty()) { reset(); return; }

    Json root;
    if (!parseJson(bytes.toStdString(), &root) || !root.isObject()) { reset(); return; }
    if (const Json* en = root.get("enabled")) enabled_ = en->asBool(true);
    if (const Json* prof = root.get("profile")) {
        // serialize()/deserialize() round-trips through the same shape; re-dump
        // the nested object back to a string for the model to parse.
        model_.deserialize(dumpJson(*prof));
    }
}

void LearnerStore::save() {
    QFileInfo info(path_);
    QDir().mkpath(info.absolutePath());

    const std::string line = "{\"enabled\":" + std::string(enabled_ ? "true" : "false") +
                             ",\"profile\":" + model_.serialize() + "}";
    QFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray::fromStdString(line));
        f.write("\n");
    }
}

QString LearnerStore::exportJson() const {
    return QString::fromStdString(model_.serialize());
}

void LearnerStore::clear() {
    model_.clear();
    enabled_ = true;
    QFile::remove(path_);
}

}  // namespace vgo
