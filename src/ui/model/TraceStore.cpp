#include "TraceStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

#include "ModelManager.h"

namespace vgo {

QString TraceStore::defaultPath() {
    return ModelManager::instance().dataDir() + QStringLiteral("/agent_trace.jsonl");
}

TraceStore::TraceStore(QString path) : path_(std::move(path)) {}

void TraceStore::append(const AgentTraceRecord& r) {
    QFileInfo info(path_);
    QDir().mkpath(info.absolutePath());

    QFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write(QByteArray::fromStdString(traceToJsonl(r)));
        f.write("\n");
    }
}

std::vector<AgentTraceRecord> TraceStore::loadRecent(int n) const {
    std::vector<AgentTraceRecord> out;
    QFile f(path_);
    if (!f.open(QIODevice::ReadOnly)) return out;

    // 逐行解析，跳过空行与损坏行；再截取最近 n 条，按时间升序返回。
    std::vector<AgentTraceRecord> parsed;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray& line : lines) {
        const QByteArray t = line.trimmed();
        if (t.isEmpty()) continue;
        AgentTraceRecord r;
        if (parseTraceRecord(t.toStdString(), &r)) parsed.push_back(std::move(r));
    }
    const std::size_t total = parsed.size();
    const std::size_t start =
        (n > 0 && total > static_cast<std::size_t>(n)) ? total - n : 0;
    for (std::size_t i = start; i < total; ++i) out.push_back(parsed[i]);
    return out;
}

void TraceStore::clear() {
    QFile::remove(path_);
}

}  // namespace vgo
