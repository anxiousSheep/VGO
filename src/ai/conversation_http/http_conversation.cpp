#include "http_conversation.h"

#include <memory>
#include <utility>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

#include "ai/explain.h"
#include "ai/gtp.h"

namespace vgo {

namespace {

QString buildSystemPrompt(const SkillRegistry& skills, QuestionClass cls, int level) {
    QString s = QStringLiteral(
        "你是一位耐心、友善的围棋老师，在陪一位棋手对弈。\n"
        "重要约束：你只能依据下面提供的事实（棋盘、目数、落点评估、死活结论）作答，"
        "绝不能编造胜率、目数、坐标或棋手意图。如果不确定，就直说“这一点我还需要进一步分析”。\n"
        "回答要简短、口语化、面向棋手，用中文。");
    // M3b.4：难度 ↔ 语气联动——级别越低讲解越细致，级别越高越简洁。
    if (level <= 6) {
        s += QStringLiteral("对方是初学者，请把术语解释清楚、多给一点思路引导。\n");
    } else if (level >= 12) {
        s += QStringLiteral("对方水平较高，直接、简洁地给出要点即可，不必解释基础术语。\n");
    }
    // N4：按问题类型注入匹配的领域知识包。
    for (const Skill* sk : skills.forQuestion(cls)) {
        s += QStringLiteral("\n【%1】%2")
                 .arg(QString::fromStdString(sk->name), QString::fromStdString(sk->prompt));
    }
    return s;
}

QString buildUserPrompt(const ConversationRequest& req) {
    QString s;
    s += QStringLiteral("棋盘（X=黑 O=白 .=空，最下方是 A 列起的坐标）：\n");
    s += QString::fromStdString(req.board_context);
    if (req.facts.analysis.valid) {
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
    if (req.facts.analysis.valid && !req.hint) {
        int shown = 0;
        for (const auto& c : req.facts.analysis.candidates) {
            if (shown >= 4) break;
            s += QStringLiteral("  候选点 %1：胜率 %2%，领先 %3 目\n")
                     .arg(QString::fromStdString(gtpPoint(c.p, req.size)))
                     .arg(c.winrate * 100.0, 0, 'f', 1)
                     .arg(c.score_lead, 0, 'f', 1);
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
    const std::string dead = renderDeadGroups(req.facts.dead_groups, req.size);
    if (!dead.empty()) s += QString::fromStdString(dead) + QStringLiteral("\n");

    s += QStringLiteral("\n棋手的问题：%1").arg(QString::fromStdString(req.question));
    return s;
}

QJsonObject buildBody(const QString& model, bool stream, const QString& systemPrompt,
                      const QString& userPrompt) {
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
    body["stream"] = stream;
    return body;
}

}  // namespace

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

    QUrl url(baseUrl_);
    if (!url.path().endsWith("/chat/completions")) {
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += "chat/completions";
        url.setPath(path);
    }

    const QuestionClass cls = classifyQuestion(req.question);
    const QJsonObject body =
        buildBody(model_, false, buildSystemPrompt(skills_, cls, req.profile.level),
                  buildUserPrompt(req));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!apiKey_.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + apiKey_).toUtf8());

    reply_ = nam_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::finished, this, [this, cb = std::move(cb)]() {
        ConversationReply r;
        if (reply_->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply_->readAll());
            const QString content = doc.object()["choices"]
                                        .toArray()
                                        .first()
                                        .toObject()["message"]
                                        .toObject()["content"]
                                        .toString();
            r.text = content.toStdString();
        } else {
            r.text = "（语言服务暂时不可用：" + reply_->errorString().toStdString() + "）";
            r.unsure = true;
        }
        reply_->deleteLater();
        reply_ = nullptr;
        cb(std::move(r));
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
        buildBody(model_, true, buildSystemPrompt(skills_, cls, req.profile.level),
                  buildUserPrompt(req));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
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
            (*sharedCb)("（语言服务暂时不可用：" + reply_->errorString().toStdString() + "）", true);
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
