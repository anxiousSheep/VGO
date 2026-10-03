#pragma once

#include <QString>

#include <vector>

#include "ai/agent/trace.h"

namespace vgo {

// D5.6 可观测：把每次问答的 AgentTraceRecord 追加到 per-user 的 agent_trace.jsonl，
// 供本地 trace 面板查看。只负责文件 I/O —— 记录的构造与序列化在 ai/agent/trace.* 里。
class TraceStore {
public:
    // 默认路径在 ModelManager::dataDir()（per-user AppData）下。
    static QString defaultPath();

    explicit TraceStore(QString path);

    void append(const AgentTraceRecord& r);       // 追加一行 JSONL
    std::vector<AgentTraceRecord> loadRecent(int n) const;  // 最近 n 条（时间升序）
    void clear();                                  // 删除文件

    QString path() const { return path_; }

private:
    QString path_;
};

}  // namespace vgo
