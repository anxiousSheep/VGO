#pragma once

#include <string>
#include <vector>

#include <QJsonObject>

namespace vgo {

// OpenAI 兼容 SSE 解析（B3，纯逻辑，Qt-Core-only，无 I/O）。累积原始字节，析出
// choices[0].delta.content 增量、choices[0].delta.tool_calls 增量，以及 [DONE] 结束标记。
// 设计为「喂字节 → drain 增量」的拉取模型，调用方按需取出并清空累积增量。
class SseParser {
public:
    // 追加原始字节（含跨块半行；按 \n 分行缓存，未完整行留在 buffer_ 等下一块）。
    void feed(const std::string& bytes);

    // 取出并清空 content 增量（choices[0].delta.content，空串不计入）。
    std::vector<std::string> drainContentDeltas();

    // 取出并清空 tool_calls 增量（每个元素为 delta.tool_calls[i] 的 QJsonObject）。
    std::vector<QJsonObject> drainToolCallDeltas();

    // 是否已看到 data: [DONE]。
    bool done() const { return done_; }

    // 最近一个非空 choices[0].finish_reason（"stop"/"length"/…），空 = 尚未出现。
    // 供调用方识别「输出因 max_tokens 被截断（length）」而重试。
    std::string finishReason() const { return finish_reason_; }

private:
    std::string buffer_;  // 未完整事件的行缓存（跨 WinHttpReadData 块）
    std::vector<std::string> content_deltas_;
    std::vector<QJsonObject> tool_call_deltas_;
    bool done_ = false;
    std::string finish_reason_;
};

}  // namespace vgo
