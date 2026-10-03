#include "http_conversation.h"

#include <memory>
#include <utility>

#include <chrono>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

#include "ai/explain.h"
#include "ai/gtp.h"

namespace vgo {

namespace {

// Wall-clock ms since epoch, for the tool-loop deadline (D1.10). Monotonic (steady).
qint64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QString buildSystemPrompt(const SkillRegistry& skills, QuestionClass cls,
                          const ConversationRequest& req) {
    QString s;
    if (req.user_color != Color::Empty) {
        // 人机对弈：有明确立场（用户执黑/白），从「对方」视角教学。
        s = QStringLiteral(
            "你是一位耐心、友善的围棋老师，在陪一位棋手对弈。\n"
            "重要约束：你只能依据下面提供的事实（棋盘、目数、落点评估、死活结论）作答，"
            "绝不能编造胜率、目数、坐标或棋手意图。如果不确定，就直说“这一点我还需要进一步分析”。\n"
            "回答要简短、口语化、面向棋手，用中文。");
        // M3b.4：难度 ↔ 语气联动——级别越低讲解越细致，级别越高越简洁。
        if (req.profile.level <= 6) {
            s += QStringLiteral("对方是初学者，请把术语解释清楚、多给一点思路引导。\n");
        } else if (req.profile.level >= 12) {
            s += QStringLiteral("对方水平较高，直接、简洁地给出要点即可，不必解释基础术语。\n");
        }
    } else {
        // 摆棋模式：无立场，从用户提问角度 / 棋盘实际情况出发，以黑方/白方称呼双方。
        s = QStringLiteral(
            "你是一位客观的围棋分析师，协助用户分析棋局。\n"
            "重要约束：你只能依据下面提供的事实（棋盘、目数、落点评估、死活结论）作答，"
            "绝不能编造胜率、目数、坐标或棋手意图。如果不确定，就直说“这一点我还需要进一步分析”。\n"
            "你没有立场：不要预设用户执黑或执白，一律从用户提问的角度、以黑方/白方称呼双方作答。\n"
            "回答要简短、口语化、用中文。");
        if (req.profile.level <= 6) {
            s += QStringLiteral("用户可能是初学者，请把术语解释清楚、多给一点思路引导。\n");
        } else if (req.profile.level >= 12) {
            s += QStringLiteral("用户水平较高，直接、简洁地给出要点即可。\n");
        }
    }
    // D3.6：注入跨局教练记忆——让 LLM 知道用户最近的弱项（如「最近在打劫上吃亏」），
    // 仅在相关时自然提及，不要突兀复述。
    if (!req.coach_memory.empty()) {
        s += QStringLiteral("教练记忆（跨局观察，仅在相关时自然提及，不要突兀复述）：%1\n")
                 .arg(QString::fromStdString(req.coach_memory));
    }
    // D2：要求模型在回答末尾附上可机读的断言块，供校验闸门对照引擎真值。
    s += QStringLiteral(
        "回答末尾请附上一段 <claims>…</claims> JSON 数组，列出你断言的所有可核验事实：\n"
        "领先目数用 {\"type\":\"score_lead\",\"value\":<目数>,\"color\":\"black\"或\"white\"}；\n"
        "胜率用 {\"type\":\"winrate\",\"value\":<0到1的小数>,\"color\":\"black\"或\"white\"}；\n"
        "某块棋死活用 {\"type\":\"group_life\",\"point\":\"<GTP坐标>\",\"alive\":true或false}。\n"
        "没有可核验的断言时可省略该块。\n");

    // N4 + D4：按问题类型 + 语义检索选择匹配的领域知识包（类别白名单优先、按 id
    // 去重、受预算裁剪），检索让「换种问法」或复合问题也能拿到相关知识。
    for (const Skill* sk : skills.select(cls, req.question, 3)) {
        s += QStringLiteral("\n【%1】%2")
                 .arg(QString::fromStdString(sk->name), QString::fromStdString(sk->prompt));
    }
    return s;
}

}  // namespace

QString buildUserPrompt(const ConversationRequest& req, QuestionClass cls) {
    QString s;
    // D1.9：Other（未命中任何专用路径）由模型走工具按需取引擎真值——prompt 只保留
    // 无引擎基础事实（棋盘 / 规则计分 / 最近一手），胜率 / 候选点 / 死棋清单改由工具取。
    const bool stripEngine = (cls == QuestionClass::Other);
    s += QStringLiteral("棋盘（X=黑 O=白 .=空，最下方是 A 列起的坐标）：\n");
    s += QString::fromStdString(req.board_context);
    if (req.facts.analysis.valid && !stripEngine) {
        const double blackWinrate = (req.to_move == Color::Black)
                                        ? req.facts.analysis.winrate
                                        : 1.0 - req.facts.analysis.winrate;
        const double blackLead = (req.to_move == Color::Black)
                                     ? req.facts.analysis.score_lead
                                     : -req.facts.analysis.score_lead;
        s += QStringLiteral("形势（KataGo 实时）：黑方胜率 %1%，黑领先 %2 目。手数：%3。\n")
                 .arg(blackWinrate * 100.0, 0, 'f', 1)
                 .arg(blackLead, 0, 'f', 1)
                 .arg(req.move_number);
    } else {
        s += QStringLiteral("形势（规则计分数子法）：黑领先 %1 目。手数：%2。\n")
                 .arg(req.score_black, 0, 'f', 1)
                 .arg(req.move_number);
    }
    if (req.user_color != Color::Empty) {
        s += QStringLiteral("棋手执%1。")
                 .arg(req.user_color == Color::Black ? QStringLiteral("黑")
                                                     : QStringLiteral("白"));
    }
    s += QStringLiteral("当前轮到%1方落子。\n")
             .arg(req.to_move == Color::Black ? QStringLiteral("黑") : QStringLiteral("白"));
    if (req.facts.analysis.valid && !req.hint && !stripEngine) {
        int shown = 0;
        for (const auto& c : req.facts.analysis.candidates) {
            if (shown >= 4) break;
            // 候选点的 winrate/score_lead 是「行棋方」视角；统一转成黑方视角，
            // 与上方「形势」行的「黑方胜率/黑领先」口径一致，避免 LLM 混淆。
            const double cWinrate =
                (req.to_move == Color::Black) ? c.winrate : 1.0 - c.winrate;
            const double cLead =
                (req.to_move == Color::Black) ? c.score_lead : -c.score_lead;
            s += QStringLiteral("  候选点 %1：黑方胜率 %2%，黑领先 %3 目\n")
                     .arg(QString::fromStdString(gtpPoint(c.p, req.size)))
                     .arg(cWinrate * 100.0, 0, 'f', 1)
                     .arg(cLead, 0, 'f', 1);
            ++shown;
        }
    }
    if (req.hint) {
        s += QStringLiteral("棋手希望得到「半提示」（方向性引导），请只给思路，不要直接说出具体坐标。\n");
    }
    if (req.has_last_move) {
        s += QStringLiteral("最近一手的坐标：%1（%2方）。\n")
                 .arg(QString::fromStdString(gtpPoint(req.last_move, req.size)))
                 .arg(req.last_mover == Color::Black ? "黑" : "白");
    }
    const std::string exp = renderExplanation(req.facts.facts, req.last_mover);
    if (!exp.empty()) {
        s += QStringLiteral("最近一手的客观描述：%1\n").arg(QString::fromStdString(exp));
    }
    // 讲解增强（N1.4）：把 what-if 落点评估 / 死活结论 / 死棋清单作为硬事实喂给 LLM。
    if (req.facts.move_eval) {
        s += QStringLiteral("落点评估（硬事实）：%1\n")
                 .arg(QString::fromStdString(renderMoveEvaluation(*req.facts.move_eval, req.size)));
    }
    if (req.facts.life_death) {
        s += QStringLiteral("死活结论（硬事实）：%1\n")
                 .arg(QString::fromStdString(renderLifeDeath(*req.facts.life_death, req.size)));
    }
    if (!stripEngine) {
        const std::string dead = renderDeadGroups(req.facts.dead_groups, req.size);
        if (!dead.empty()) s += QString::fromStdString(dead) + QStringLiteral("\n");
    }

    // 试下模式上下文：让模型知道已有进行中的演示，从而正确判断继续/重置/回退。
    if (req.in_demo) {
        s += QStringLiteral("注意：当前已有一个进行中的棋盘演示（试下模式），已下的手按编号为：%1\n"
                            "· 用户说「继续/接着/再演示」→ 调用演示工具，reset=false（在当前基础上续下）。\n"
                            "· 用户说「重新假设/重下/改第 k 手」或要回到某一步 → 调用演示工具，"
                            "undo_to = k（回退到第 k 手之前），再落新子续下。\n"
                            "· 用户明确说「完全重新/从头」→ reset=true。\n")
                 .arg(req.demo_moves.empty() ? QStringLiteral("（尚未落子）")
                                             : QString::fromStdString(req.demo_moves));
    }

    s += QStringLiteral("\n棋手的问题：%1").arg(QString::fromStdString(req.question));
    return s;
}

namespace {

QJsonObject buildBody(const QString& model, bool stream, const QString& systemPrompt,
                      const QString& userPrompt, int max_tokens) {
    QJsonArray messages;
    QJsonObject system;
    system["role"] = "system";
    system["content"] = systemPrompt;
    messages.append(system);
    QJsonObject user;
    user["role"] = "user";
    user["content"] = userPrompt;
    messages.append(user);

    QJsonObject body;
    body["model"] = model;
    body["messages"] = messages;
    body["max_tokens"] = max_tokens;
    body["stream"] = stream;
    return body;
}

// The tools exposed to the model via function-calling. The model decides when
// to call each and with which arguments (parsing natural language). The demo
// tool is always declared (UI-owned); the injected ToolRegistry (D1) adds the
// deterministic capabilities: count_territory / life_death_solve / ownership_map /
// dead_groups / analyze_position.
QJsonArray buildTools(const ToolRegistry* reg) {
    QJsonObject fn;
    fn["name"] = "demonstrate_go_moves";
    fn["description"] = QStringLiteral(
        "在棋盘上演示走法：进入试下模式，可回退若干步、落一手假设手，并用权威 AI 续下若干手。"
        "当用户要求演示、展示变化、模拟对局、询问后续应对、局部变化或「后面几步怎么下」时调用。");

    QJsonObject pPoint;
    pPoint["type"] = "string";
    pPoint["description"] = QStringLiteral(
        "假设手的 GTP 坐标（如 H6），必须是当前棋盘上的空点；无明确落点或不确定时为空字符串");
    QJsonObject pColor;
    pColor["type"] = "string";
    pColor["enum"] = QJsonArray{"black", "white", ""};
    pColor["description"] = QStringLiteral("假设手的执子方（black/white），无落点可省略");
    QJsonObject pPlies;
    pPlies["type"] = "integer";
    pPlies["description"] = QStringLiteral("权威 AI 续下的手数，默认 4，范围 1-8");
    QJsonObject pReset;
    pReset["type"] = "boolean";
    pReset["description"] = QStringLiteral(
        "true 表示从头重新演示（丢弃当前变化图）；false 或省略表示在当前演示基础上继续");
    QJsonObject pUndo;
    pUndo["type"] = "integer";
    pUndo["description"] = QStringLiteral(
        "回退到第 k 手之前（保留前 k-1 手，撤销第 k 手及之后的所有手）。"
        "例如用户说「重新假设/改第9手」就传 9；省略或 0 = 不回退");

    QJsonObject props;
    props["point"] = pPoint;
    props["color"] = pColor;
    props["plies"] = pPlies;
    props["reset"] = pReset;
    props["undo_to"] = pUndo;

    QJsonObject params;
    params["type"] = "object";
    params["properties"] = props;
    fn["parameters"] = params;

    QJsonObject tool;
    tool["type"] = "function";
    tool["function"] = fn;

    QJsonArray tools;
    tools.append(tool);

    if (reg) {
        for (const Tool& t : reg->all()) {
            QJsonObject f;
            f["name"] = QString::fromStdString(t.name);
            f["description"] = QString::fromStdString(t.description);
            f["parameters"] =
                QJsonDocument::fromJson(QByteArray::fromStdString(t.json_schema)).object();
            QJsonObject o;
            o["type"] = "function";
            o["function"] = f;
            tools.append(o);
        }
    }
    return tools;
}

// 把 QNetworkReply 的错误翻译成对用户有用的中文：区分超时/取消、HTTP 状态码，并从
// OpenAI 兼容端点常见的 JSON 错误体（{"error":{"message":…}}）里抽出 message。
QString httpErrorDetail(QNetworkReply* reply) {
    if (!reply || reply->error() == QNetworkReply::NoError) return {};
    const auto err = reply->error();
    if (err == QNetworkReply::TimeoutError) return QStringLiteral("请求超时（可稍后重试）");
    if (err == QNetworkReply::OperationCanceledError) return QStringLiteral("请求被取消");
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QString msg;
    if (doc.isObject()) {
        msg = doc.object().value("error").toObject().value("message").toString();
    }
    if (!msg.isEmpty()) {
        return QStringLiteral("服务返回 %1：%2").arg(status).arg(msg);
    }
    if (status > 0) {
        return QStringLiteral("服务返回 HTTP %1").arg(status);
    }
    return reply->errorString();
}

}  // namespace

QJsonArray buildToolFollowup(const QJsonArray& messages, const QString& content,
                             const QJsonArray& toolCalls,
                             const std::function<std::string(const QJsonObject&)>& exec) {
    QJsonArray next = messages;
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    if (!content.isEmpty()) assistantMsg["content"] = content;
    assistantMsg["tool_calls"] = toolCalls;
    next.append(assistantMsg);
    for (const QJsonValue& tv : toolCalls) {
        const QJsonObject call = tv.toObject();
        QJsonObject toolMsg;
        toolMsg["role"] = "tool";
        toolMsg["tool_call_id"] = call["id"].toString();
        toolMsg["content"] = QString::fromStdString(exec(call));
        next.append(toolMsg);
    }
    return next;
}

HttpConversationService::HttpConversationService(const QString& baseUrl, const QString& apiKey,
                                                 const QString& model, QObject* parent)
    : QObject(parent), baseUrl_(baseUrl), apiKey_(apiKey), model_(model) {
    available_ = !baseUrl_.isEmpty() && !model_.isEmpty();
}

HttpConversationService::~HttpConversationService() = default;

void HttpConversationService::answer(const ConversationRequest& req, Callback cb) {
    if (!available_) {
        cb(ConversationReply{});
        return;
    }
    const QuestionClass cls = classifyQuestion(req.question);
    QJsonArray messages;
    QJsonObject system;
    system["role"] = "system";
    system["content"] = buildSystemPrompt(skills_, cls, req);
    messages.append(system);
    QJsonObject user;
    user["role"] = "user";
    user["content"] = buildUserPrompt(req, cls);
    messages.append(user);
    doChat(messages, 0, nowMs() + max_loop_ms_, std::move(cb));
}

void HttpConversationService::doChat(QJsonArray messages, int depth, qint64 deadline_ms,
                                     Callback cb) {
    QUrl url(baseUrl_);
    if (!url.path().endsWith("/chat/completions")) {
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += "chat/completions";
        url.setPath(path);
    }

    QJsonObject body;
    body["model"] = model_;
    body["messages"] = messages;
    // D1.10：depth 上限 + 墙钟预算双界——超时后本轮不再声明工具，模型必须给出最终
    // 文本答案，环路自然终止。
    if ((tool_handler_ || !tool_registry_.all().empty()) && depth < 4 &&
        nowMs() < deadline_ms)
        body["tools"] = buildTools(&tool_registry_);
    body["max_tokens"] = max_tokens_;  // D5.5：单次回答 token 上限
    body["stream"] = false;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(timeout_ms_);  // D5.5：超时熔断
    if (!apiKey_.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + apiKey_).toUtf8());

    reply_ = nam_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::finished, this,
            [this, messages, depth, deadline_ms, cb = std::move(cb)]() mutable {
                ConversationReply r;
                if (reply_->error() != QNetworkReply::NoError) {
                    r.text = "（语言服务暂时不可用：" + httpErrorDetail(reply_).toStdString() +
                             "）";
                    r.unsure = true;
                    reply_->deleteLater();
                    reply_ = nullptr;
                    cb(std::move(r));
                    return;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(reply_->readAll());
                const QJsonObject msg = doc.object()["choices"]
                                            .toArray()
                                            .first()
                                            .toObject()["message"]
                                            .toObject();
                const QJsonArray toolCalls = msg["tool_calls"].toArray();
                const QString content = msg["content"].toString();
                reply_->deleteLater();
                reply_ = nullptr;

                if (toolCalls.isEmpty() ||
                    (!tool_handler_ && tool_registry_.all().empty())) {
                    r.text = content.toStdString();
                    cb(std::move(r));
                    return;
                }

                // Execute every requested tool call, then feed each result back
                // and ask the model for the final natural-language answer. Every
                // tool_call must be answered by a matching `tool` message (same
                // tool_call_id), or the endpoint rejects the follow-up request.
                auto exec = [&](const QJsonObject& call) -> std::string {
                    const QString fnName = call["function"].toObject()["name"].toString();
                    const QJsonValue av = call["function"].toObject()["arguments"];
                    QJsonObject args;
                    if (av.isString())
                        args = QJsonDocument::fromJson(av.toString().toUtf8()).object();
                    else if (av.isObject())
                        args = av.toObject();

                    // D1：先查注入的工具注册表（确定性能力），再回退到 UI 的演示工具。
                    if (const Tool* t = tool_registry_.find(fnName.toStdString())) {
                        const std::string argsJson =
                            QJsonDocument(args).toJson(QJsonDocument::Compact).toStdString();
                        const ToolResult tr = t->invoke(argsJson);
                        return tr.ok ? tr.json : ("工具执行失败：" + tr.error);
                    }
                    if (tool_handler_) return tool_handler_(fnName, args);
                    return "未知工具";
                };

                const QJsonArray next = buildToolFollowup(messages, content, toolCalls, exec);
                doChat(next, depth + 1, deadline_ms, std::move(cb));
            });
}

void HttpConversationService::answerStream(const ConversationRequest& req, DeltaCallback cb) {
    if (!available_) {
        cb("", true);
        return;
    }

    QUrl url(baseUrl_);
    if (!url.path().endsWith("/chat/completions")) {
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += "chat/completions";
        url.setPath(path);
    }

    const QuestionClass cls = classifyQuestion(req.question);
    const QJsonObject body =
        buildBody(model_, true, buildSystemPrompt(skills_, cls, req),
                  buildUserPrompt(req, cls), max_tokens_);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(timeout_ms_);  // D5.5：超时熔断
    if (!apiKey_.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + apiKey_).toUtf8());

    auto sharedCb = std::make_shared<DeltaCallback>(std::move(cb));
    stream_buffer_.clear();
    reply_ = nam_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    // Process complete SSE `data:` lines; a line may arrive split across chunks.
    auto processBuffer = [sharedCb](QByteArray& buf) {
        int nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
            const QByteArray line = buf.left(nl).trimmed();
            buf.remove(0, nl + 1);
            if (!line.startsWith("data:")) continue;
            const QByteArray payload = line.mid(5).trimmed();
            if (payload == "[DONE]" || payload.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const QJsonObject ch = doc.object()["choices"].toArray().first().toObject();
            const QString content = ch.contains("delta")
                                        ? ch["delta"].toObject()["content"].toString()
                                        : ch["message"].toObject()["content"].toString();
            if (!content.isEmpty()) (*sharedCb)(content.toStdString(), false);
        }
    };

    connect(reply_, &QNetworkReply::readyRead, this, [this, processBuffer]() {
        stream_buffer_ += reply_->readAll();
        processBuffer(stream_buffer_);
    });

    connect(reply_, &QNetworkReply::finished, this, [this, processBuffer, sharedCb]() {
        stream_buffer_ += '\n';  // flush a final unterminated `data:` line
        processBuffer(stream_buffer_);
        if (reply_->error() != QNetworkReply::NoError) {
            (*sharedCb)("（语言服务暂时不可用：" + httpErrorDetail(reply_).toStdString() + "）", true);
        } else {
            (*sharedCb)("", true);
        }
        reply_->deleteLater();
        reply_ = nullptr;
    });
}

void HttpConversationService::cancel() {
    if (reply_) reply_->abort();
}

}  // namespace vgo
