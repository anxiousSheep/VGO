#include "AppConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

AppConfig loadAppConfig(const QString& dir) {
    AppConfig cfg;
    const QString path = dir + QStringLiteral("/vgo_config.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return cfg;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();
    if (const QJsonObject k = root.value("katago").toObject(); !k.isEmpty()) {
        cfg.katago.enabled = k.value("enabled").toBool(false);
        cfg.katago.binary = k.value("binary").toString();
        cfg.katago.model = k.value("model").toString();
        cfg.katago.config = k.value("config").toString();
    }
    if (const QJsonObject l = root.value("llm").toObject(); !l.isEmpty()) {
        cfg.llm.enabled = l.value("enabled").toBool(false);
        cfg.llm.baseUrl = l.value("base_url").toString();
        cfg.llm.apiKey = l.value("api_key").toString();
        cfg.llm.model = l.value("model").toString();
    }
    return cfg;
}
