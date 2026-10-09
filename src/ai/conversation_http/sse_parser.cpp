#include "sse_parser.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>

namespace vgo {

void SseParser::feed(const std::string& bytes) {
    buffer_ += bytes;
    std::size_t pos;
    while ((pos = buffer_.find('\n')) != std::string::npos) {
        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();  // 兼容 \r\n

        // 只关心 data: 行（event:/id:/retry: 等忽略）。
        if (line.rfind("data:", 0) != 0) continue;
        std::string payload = line.substr(5);
        std::size_t i = 0;
        while (i < payload.size() && payload[i] == ' ') ++i;
        payload = payload.substr(i);

        if (payload == "[DONE]") {
            done_ = true;
            continue;
        }

        const QJsonDocument doc =
            QJsonDocument::fromJson(QByteArray::fromStdString(payload));
        if (!doc.isObject()) continue;
        const QJsonArray choices = doc.object().value("choices").toArray();
        if (choices.isEmpty()) continue;
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject delta = choice.value("delta").toObject();

        const QJsonValue fr = choice.value("finish_reason");
        if (fr.isString() && !fr.toString().isEmpty())
            finish_reason_ = fr.toString().toStdString();

        const QJsonValue c = delta.value("content");
        if (c.isString() && !c.toString().isEmpty())
            content_deltas_.push_back(c.toString().toStdString());

        const QJsonValue tc = delta.value("tool_calls");
        if (tc.isArray()) {
            for (const QJsonValue& v : tc.toArray())
                if (v.isObject()) tool_call_deltas_.push_back(v.toObject());
        }
    }
}

std::vector<std::string> SseParser::drainContentDeltas() {
    std::vector<std::string> out;
    out.swap(content_deltas_);
    return out;
}

std::vector<QJsonObject> SseParser::drainToolCallDeltas() {
    std::vector<QJsonObject> out;
    out.swap(tool_call_deltas_);
    return out;
}

}  // namespace vgo
