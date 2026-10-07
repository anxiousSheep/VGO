#include "bad_sample.h"

#include "../json.h"

namespace vgo {

std::string badSampleToJsonl(const BadSample& s) {
    return dumpJson(Json::makeObject({
        {"ts", Json::makeNumber(static_cast<double>(s.ts_ms))},
        {"position_hash", Json::makeString(s.position_hash)},
        {"question", Json::makeString(s.question)},
        {"reply_text", Json::makeString(s.reply_text)},
        {"disposition", Json::makeString(s.disposition)},
        {"truth_summary", Json::makeString(s.truth_summary)},
        {"model", Json::makeString(s.model)},
    }));
}

bool parseBadSample(const std::string& text, BadSample* out) {
    if (!out) return false;
    Json j;
    if (!parseJson(text, &j) || !j.isObject()) return false;
    auto num = [&](const char* k, double def) {
        const Json* v = j.get(k);
        return v ? v->asNumber(def) : def;
    };
    auto str = [&](const char* k, const std::string& def) {
        const Json* v = j.get(k);
        return v ? v->asString(def) : def;
    };
    out->ts_ms = static_cast<std::int64_t>(num("ts", 0.0));
    out->position_hash = str("position_hash", "");
    out->question = str("question", "");
    out->reply_text = str("reply_text", "");
    out->disposition = str("disposition", "");
    out->truth_summary = str("truth_summary", "");
    out->model = str("model", "");
    return true;
}

}  // namespace vgo
