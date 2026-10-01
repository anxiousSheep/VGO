#include "http_conversation.h"

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

QString buildSystemPrompt() {
    return QStringLiteral(
        "你是一位耐心、友善的围棋老师，在陪一位初学者下棋。\n"
        "重要约束：你只能依据下面提供的事实（棋盘、目数、最近一手的客观描述）作答，"
        "绝不能编造胜率、目数、坐标或棋手意图。如果不确定，就直说“这一点我还需要进一步分析”。\n"
        "回答要简短、口语化、面向新手，用中文。");
}

QString buildUserPrompt(const ConversationRequest& req) {
    QString s;
    s += QStringLiteral("棋盘（X=黑 O=白 .=空，最下方是 A 列起的坐标）：\n");
    s += QString::fromStdString(req.board_context);
    s += QStringLiteral("当前形势：黑领先 %1 目（含贴目）。手数：%2。\n")
             .arg(req.score_black, 0, 'f', 1)
             .arg(req.move_number);
    if (req.user_color != Color::Empty) {
        s += QStringLiteral("棋手执%1。")
                 .arg(req.user_color == Color::Black ? QStringLiteral("黑")
                                                     : QStringLiteral("白"));
    }
    s += QStringLiteral("当前轮到%1方落子。\n")
             .arg(req.to_move == Color::Black ? QStringLiteral("黑") : QStringLiteral("白"));
    if (req.has_last_move) {
        s += QStringLiteral("最近一手的坐标：%1（%2方）。\n")
                 .arg(QString::fromStdString(gtpPoint(req.last_move, req.size)))
                 .arg(req.last_mover == Color::Black ? "黑" : "白");
    }
    const std::string exp = renderExplanation(req.facts.facts, req.last_mover);
    if (!exp.empty()) {
        s += QStringLiteral("最近一手的客观描述：%1\n").arg(QString::fromStdString(exp));
    }
    s += QStringLiteral("\n棋手的问题：%1").arg(QString::fromStdString(req.question));
    return s;
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

    QJsonArray messages;
    QJsonObject system;
    system["role"] = "system";
    system["content"] = buildSystemPrompt();
    messages.append(system);
    QJsonObject user;
    user["role"] = "user";
    user["content"] = buildUserPrompt(req);
    messages.append(user);

    QJsonObject body;
    body["model"] = model_;
    body["messages"] = messages;
    body["stream"] = false;

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

void HttpConversationService::cancel() {
    if (reply_) reply_->abort();
}

}  // namespace vgo
