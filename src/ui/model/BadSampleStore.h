#pragma once

#include <QString>

#include <vector>

#include "ai/verify/bad_sample.h"

namespace vgo {

// D2.5：把校验闸门拦截到的「坏样本」追加到 per-user 的 bad_samples.jsonl，形成可复
// 查的坏样本集，反哺 D4 的评估闭环（golden 集扩充 / skill 迭代）。只负责文件 I/O ——
// 样本的构造与序列化在 ai/verify/bad_sample.* 里。
class BadSampleStore {
public:
    // 默认路径在 ModelManager::dataDir()（per-user AppData）下。
    static QString defaultPath();

    explicit BadSampleStore(QString path);

    void append(const BadSample& s);              // 追加一行 JSONL
    std::vector<BadSample> loadRecent(int n) const;  // 最近 n 条（时间升序）
    void clear();                                 // 删除文件

    QString path() const { return path_; }

private:
    QString path_;
};

}  // namespace vgo
