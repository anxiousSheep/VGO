#include "BadSampleStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

#include "ModelManager.h"

namespace vgo {

QString BadSampleStore::defaultPath() {
    return ModelManager::instance().dataDir() + QStringLiteral("/bad_samples.jsonl");
}

BadSampleStore::BadSampleStore(QString path) : path_(std::move(path)) {}

void BadSampleStore::append(const BadSample& s) {
    QFileInfo info(path_);
    QDir().mkpath(info.absolutePath());

    QFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write(QByteArray::fromStdString(badSampleToJsonl(s)));
        f.write("\n");
    }
}

std::vector<BadSample> BadSampleStore::loadRecent(int n) const {
    std::vector<BadSample> out;
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) return out;

    std::vector<BadSample> parsed;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray& line : lines) {
        const QByteArray t = line.trimmed();
        if (t.isEmpty()) continue;
        BadSample s;
        if (parseBadSample(t.toStdString(), &s)) parsed.push_back(std::move(s));
    }
    const std::size_t total = parsed.size();
    const std::size_t start =
        (n > 0 && total > static_cast<std::size_t>(n)) ? total - n : 0;
    for (std::size_t i = start; i < total; ++i) out.push_back(parsed[i]);
    return out;
}

void BadSampleStore::removeByTimestamp(std::int64_t ts_ms) {
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QList<QByteArray> lines = f.readAll().split('\n');
    f.close();

    QFile out(path_);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    for (const QByteArray& raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty()) continue;
        BadSample s;
        if (parseBadSample(line.toStdString(), &s) && s.ts_ms == ts_ms) continue;
        out.write(line);
        out.write("\n");
    }
}

void BadSampleStore::clear() {
    QFile::remove(path_);
}

}  // namespace vgo
