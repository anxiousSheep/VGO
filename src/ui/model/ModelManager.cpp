#include "ModelManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "KeyStore.h"

namespace vgo {

namespace {
QString g_overrideDir;
}

ModelManager& ModelManager::instance() {
    static ModelManager m;
    return m;
}

void ModelManager::setDataDirOverride(const QString& dir) {
    g_overrideDir = dir;
    // 测试钩子：换目录时重置缓存态，使下一次 load() 重新读取。
    ModelManager& m = instance();
    m.loaded_ = false;
    m.katago_ = KataGoSettings{};
    m.llm_ = LlmSettings{};
    m.apiKeyCache_.clear();
    m.keyLoaded_ = false;
    m.onboardingDone_ = false;
}

QString ModelManager::dataDir() const {
    if (!g_overrideDir.isEmpty()) return g_overrideDir;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    return dir;
}

QString ModelManager::configPath() const {
    return dataDir() + QStringLiteral("/config.json");
}

QString ModelManager::secretsPath() const {
    return dataDir() + QStringLiteral("/secrets.json");
}

void ModelManager::load() {
    if (loaded_) return;
    loaded_ = true;
    QDir().mkpath(dataDir());
    migrateLegacy();
    readConfig();
    readSecrets();
}

void ModelManager::ensureLoaded() {
    load();
}

void ModelManager::migrateLegacy() {
    if (QFileInfo::exists(configPath())) return;  // already migrated
    const QString legacy =
        QCoreApplication::applicationDirPath() + QStringLiteral("/vgo_config.json");
    QFile f(legacy);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (const QJsonObject k = root.value("katago").toObject(); !k.isEmpty()) {
        katago_.enabled = k.value("enabled").toBool(false);
        katago_.binary = k.value("binary").toString();
        katago_.model = k.value("model").toString();
        katago_.config = k.value("config").toString();
    }
    if (const QJsonObject l = root.value("llm").toObject(); !l.isEmpty()) {
        llm_.baseUrl = l.value("base_url").toString();
        llm_.model = l.value("model").toString();
        const QString key = l.value("api_key").toString();
        if (!key.isEmpty()) setApiKey(key);  // 迁移时即加密
    }
    saveConfig();
}

void ModelManager::readConfig() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (const QJsonObject k = root.value("katago").toObject(); !k.isEmpty()) {
        katago_.enabled = k.value("enabled").toBool(true);
        katago_.binary = k.value("binary").toString();
        katago_.model = k.value("model").toString();
        katago_.humanModel = k.value("human_model").toString();
        katago_.config = k.value("config").toString();
        katago_.version = k.value("version").toString();
        katago_.variant = k.value("variant").toString();
    }
    if (const QJsonObject l = root.value("llm").toObject(); !l.isEmpty()) {
        llm_.provider = l.value("provider").toString();
        llm_.baseUrl = l.value("base_url").toString();
        llm_.model = l.value("model").toString();
    }
    onboardingDone_ = root.value("onboarding_done").toBool(false);
}

void ModelManager::saveConfig() {
    QDir().mkpath(dataDir());
    QJsonObject k;
    k["enabled"] = katago_.enabled;
    k["binary"] = katago_.binary;
    k["model"] = katago_.model;
    k["human_model"] = katago_.humanModel;
    k["config"] = katago_.config;
    k["version"] = katago_.version;
    k["variant"] = katago_.variant;

    QJsonObject l;
    l["provider"] = llm_.provider;
    l["base_url"] = llm_.baseUrl;
    l["model"] = llm_.model;

    QJsonObject root;
    root["katago"] = k;
    root["llm"] = l;
    root["onboarding_done"] = onboardingDone_;

    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void ModelManager::readSecrets() {
    QFile f(secretsPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QString enc = root.value("llm").toObject().value("api_key_enc").toString();
    if (!enc.isEmpty()) {
        // 关键：落盘的是 DPAPI 密文，读回时必须解密，否则会把密文当 key 传给 LLM。
        const QByteArray dec = KeyStore::unprotect(QByteArray::fromBase64(enc.toLatin1()));
        apiKeyCache_ = QString::fromUtf8(dec);
        keyLoaded_ = true;
    }
}

void ModelManager::saveSecrets() {
    QDir().mkpath(dataDir());
    QJsonObject root;
    QJsonObject l;
    if (!apiKeyCache_.isEmpty()) {
        const QByteArray enc = KeyStore::protect(apiKeyCache_.toUtf8());
        l["api_key_enc"] = QString::fromLatin1(enc.toBase64());
    }
    root["llm"] = l;
    QFile f(secretsPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void ModelManager::setKatago(const KataGoSettings& s) {
    ensureLoaded();
    katago_ = s;
    saveConfig();
}

void ModelManager::setLlm(const LlmSettings& s) {
    ensureLoaded();
    llm_ = s;
    saveConfig();
}

QString ModelManager::apiKey() const {
    return apiKeyCache_;
}

void ModelManager::setApiKey(const QString& key) {
    ensureLoaded();
    apiKeyCache_ = key;
    keyLoaded_ = true;
    saveSecrets();
}

bool ModelManager::onboardingDone() const {
    return onboardingDone_;
}

void ModelManager::setOnboardingDone(bool done) {
    ensureLoaded();
    onboardingDone_ = done;
    saveConfig();
}

bool ModelManager::katagoReady() const {
    return katago_.enabled && !katago_.binary.isEmpty() && !katago_.model.isEmpty() &&
           QFileInfo::exists(katago_.binary) && QFileInfo::exists(katago_.model);
}

QString ModelManager::katagoStatusText() const {
    if (!katago_.enabled) return QStringLiteral("未启用");
    if (katago_.binary.isEmpty() || katago_.model.isEmpty()) return QStringLiteral("未安装");
    if (!QFileInfo::exists(katago_.binary)) return QStringLiteral("缺少 katago.exe");
    if (!QFileInfo::exists(katago_.model)) return QStringLiteral("缺少模型文件");
    QString v = katago_.version.isEmpty() ? QStringLiteral("已安装")
                                          : QStringLiteral("v") + katago_.version;
    if (!katago_.variant.isEmpty()) v += QStringLiteral(" (") + katago_.variant + QStringLiteral(")");
    return v;
}

QString ModelManager::llmStatusText() const {
    if (llm_.baseUrl.isEmpty() || llm_.model.isEmpty()) return QStringLiteral("未配置");
    QString name = llm_.provider.isEmpty() ? llm_.baseUrl : llm_.provider;
    return name + QStringLiteral(" · ") + llm_.model;
}

}  // namespace vgo
