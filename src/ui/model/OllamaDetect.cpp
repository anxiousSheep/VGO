#include "OllamaDetect.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace vgo {

OllamaDetect::OllamaDetect(QObject* parent) : QObject(parent) {}

void OllamaDetect::detect() {
    QNetworkRequest req(QUrl(QStringLiteral("http://localhost:11434/api/tags")));
    req.setTransferTimeout(1500);
    reply_ = nam_.get(req);
    connect(reply_, &QNetworkReply::finished, this, [this]() {
        QStringList models;
        if (reply_->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(reply_->readAll()).object();
            const QJsonArray arr = root.value("models").toArray();
            for (const auto& v : arr) models << v.toObject().value("name").toString();
        }
        const bool ok = !models.isEmpty();
        reply_->deleteLater();
        reply_ = nullptr;
        if (ok) emit detected(models);
        else emit failed(QStringLiteral("未检测到本地 Ollama（未运行或未安装）"));
    });
}

}  // namespace vgo
