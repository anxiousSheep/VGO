#include "test_framework.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ui/model/KeyStore.h"
#include "ui/model/ModelManager.h"
#include "ui/model/ModelsManifest.h"

#include <QProcess>

using namespace vgo;

TEST(qprocess_startcommand_works) {
    QProcess p;
    p.startCommand(QStringLiteral("powershell.exe -NoProfile -Command \"Write-Output VGO_OK\""));
    CHECK(p.waitForFinished(8000));
    CHECK(p.readAllStandardOutput().contains("VGO_OK"));
}

TEST(manifest_sanity) {
    const auto models = modelAssets();
    CHECK(models.size() >= 1);
    for (const auto& m : models) {
        CHECK(!m.id.empty());
        CHECK(!m.officialUrl.empty());
        CHECK(!m.mirrors.empty());
    }
    const auto assets = katagoAssets();
    CHECK(assets.size() >= 2);
    for (const auto& a : assets) {
        CHECK(!a.officialUrl.empty());
        CHECK(!a.mirrors.empty());
    }
}

TEST(model_manager_roundtrip_no_plaintext_key) {
    QTemporaryDir dir;
    ModelManager::setDataDirOverride(dir.path());
    ModelManager& m = ModelManager::instance();
    m.load();

    KataGoSettings k;
    k.enabled = true;
    k.binary = dir.path() + QStringLiteral("/katago.exe");
    k.model = dir.path() + QStringLiteral("/model.bin.gz");
    k.version = QStringLiteral("1.15.3");
    k.variant = QStringLiteral("eigen");
    m.setKatago(k);

    LlmSettings l;
    l.provider = QStringLiteral("deepseek");
    l.baseUrl = QStringLiteral("https://api.deepseek.com/v1");
    l.model = QStringLiteral("deepseek-chat");
    m.setLlm(l);
    m.setApiKey(QStringLiteral("sk-secret-9999"));

    CHECK(m.katago().binary == k.binary);
    CHECK(m.katago().variant == QStringLiteral("eigen"));
    CHECK(m.llm().provider == QStringLiteral("deepseek"));
    CHECK(m.apiKey() == QStringLiteral("sk-secret-9999"));
    CHECK(QFile::exists(dir.path() + QStringLiteral("/config.json")));
    CHECK(QFile::exists(dir.path() + QStringLiteral("/secrets.json")));

    // secrets.json 不应包含明文 key。
    QFile sf(dir.path() + QStringLiteral("/secrets.json"));
    CHECK(sf.open(QIODevice::ReadOnly));
    CHECK(!sf.readAll().contains("sk-secret-9999"));
}

TEST(model_manager_key_decrypts_on_reload) {
    QTemporaryDir dir;
    ModelManager::setDataDirOverride(dir.path());
    // 预写 secrets.json（模拟上次会话 setApiKey 落盘的 DPAPI 密文）。
    const QByteArray enc = KeyStore::protect("sk-disk-test-123");
    QJsonObject llm;
    llm["api_key_enc"] = QString::fromLatin1(enc.toBase64());
    QJsonObject root;
    root["llm"] = llm;
    QFile f(dir.path() + QStringLiteral("/secrets.json"));
    CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(root).toJson());
    f.close();

    ModelManager::instance().load();
    // 关键：读回后必须解密，返回明文 key，而非密文。
    CHECK(ModelManager::instance().apiKey() == QStringLiteral("sk-disk-test-123"));
}

TEST(model_manager_status_uninstalled) {
    QTemporaryDir dir;
    ModelManager::setDataDirOverride(dir.path());
    ModelManager::instance().load();
    // 全新目录：未装 KataGo、未配 LLM。
    CHECK(!ModelManager::instance().katagoReady());
    CHECK(ModelManager::instance().katagoStatusText() == QStringLiteral("未安装"));
    CHECK(ModelManager::instance().llmStatusText() == QStringLiteral("未配置"));
}
