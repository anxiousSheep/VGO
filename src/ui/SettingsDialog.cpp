#include "SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ai/llm_presets.h"
#include "ai/strength.h"
#include "model/GpuDetect.h"
#include "model/KeyStore.h"
#include "model/KataGoProvisioner.h"
#include "model/ModelManager.h"
#include "model/ModelsManifest.h"
#include "model/OllamaDetect.h"

using namespace vgo;

namespace {

QString ensureChatCompletions(QString baseUrl) {
    if (baseUrl.isEmpty()) return baseUrl;
    QString u = baseUrl;
    while (u.endsWith('/')) u.chop(1);
    if (!u.endsWith("/chat/completions")) u += "/chat/completions";
    return u;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("模型设置"));
    resize(560, 560);

    nam_ = new QNetworkAccessManager(this);

    auto* root = new QVBoxLayout(this);
    buildKataGoGroup();
    buildLlmGroup();
    buildAnalysisGroup();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    root->addWidget(buttons);

    refreshKataGoStatus();
    refreshLlmFields();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::buildKataGoGroup() {
    auto* box = new QGroupBox(QStringLiteral("KataGo 引擎"), this);
    auto* form = new QFormLayout(box);

    katagoStatus_ = new QLabel(box);

    const GpuKind gpu = detectGpuKind();
    auto* gpuLabel = new QLabel(QStringLiteral("检测到：") + gpuKindName(gpu), box);

    variantCombo_ = new QComboBox(box);
    variantCombo_->addItem(QStringLiteral("OpenCL（GPU，推荐）"), QStringLiteral("opencl"));
    variantCombo_->addItem(QStringLiteral("Eigen（CPU，保底）"), QStringLiteral("eigen"));
    variantCombo_->addItem(QStringLiteral("CUDA（NVIDIA，高级）"), QStringLiteral("cuda"));
    variantCombo_->setCurrentIndex(gpu == GpuKind::OpenCl ? 0 : 1);

    installBtn_ = new QPushButton(QStringLiteral("安装/更新 KataGo 引擎"), box);
    progress_ = new QProgressBar(box);
    progress_->setVisible(false);

    form->addRow(QStringLiteral("状态"), katagoStatus_);
    form->addRow(QStringLiteral("变体"), gpuLabel);
    form->addRow(QStringLiteral("下载变体"), variantCombo_);
    form->addRow(installBtn_);
    form->addRow(progress_);

    // 模型列表
    const auto models = modelAssets();
    for (std::size_t i = 0; i < models.size() && i < 3; ++i) {
        auto* row = new QWidget(box);
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(QString::fromStdString(models[i].name) + QStringLiteral("\n") +
                                     QString::fromStdString(models[i].description),
                                 row);
        hl->addWidget(label, 1);
        auto* btn = new QPushButton(QStringLiteral("安装"), row);
        const QString id = QString::fromStdString(models[i].id);
        connect(btn, &QPushButton::clicked, this,
                [this, id] { startInstall(variantCombo_->currentData().toString(), id); });
        hl->addWidget(btn);
        modelButtons_[i] = btn;
        form->addRow(row);
    }

    connect(installBtn_, &QPushButton::clicked, this, [this] {
        startInstall(variantCombo_->currentData().toString(), QString());
    });

    auto* v = qobject_cast<QVBoxLayout*>(layout());
    v->addWidget(box);
}

void SettingsDialog::buildLlmGroup() {
    auto* box = new QGroupBox(QStringLiteral("LLM 对话（OpenAI 兼容）"), this);
    auto* form = new QFormLayout(box);

    providerCombo_ = new QComboBox(box);
    for (const auto& p : llmPresets()) {
        providerCombo_->addItem(QString::fromStdString(p.name), QString::fromStdString(p.id));
    }
    baseUrlEdit_ = new QLineEdit(box);
    modelEdit_ = new QLineEdit(box);
    keyEdit_ = new QLineEdit(box);
    keyEdit_->setEchoMode(QLineEdit::Password);

    auto* detectBtn = new QPushButton(QStringLiteral("检测本地 Ollama"), box);
    auto* testBtn = new QPushButton(QStringLiteral("测试连接"), box);
    llmResult_ = new QLabel(QStringLiteral("（尚未测试）"), box);
    llmResult_->setWordWrap(true);

    auto* row = new QWidget(box);
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(detectBtn);
    hl->addWidget(testBtn);
    hl->addStretch(1);

    form->addRow(QStringLiteral("Provider"), providerCombo_);
    form->addRow(QStringLiteral("Base URL"), baseUrlEdit_);
    form->addRow(QStringLiteral("模型"), modelEdit_);
    form->addRow(QStringLiteral("API Key"), keyEdit_);
    form->addRow(row);
    form->addRow(QStringLiteral("结果"), llmResult_);

    connect(providerCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString id = providerCombo_->currentData().toString();
        const LlmPreset* p = findPreset(id.toStdString());
        if (!p) return;
        if (id != QStringLiteral("custom") && !p->baseUrl.empty())
            baseUrlEdit_->setText(QString::fromStdString(p->baseUrl));
        if (id != QStringLiteral("custom") && !p->defaultModel.empty())
            modelEdit_->setText(QString::fromStdString(p->defaultModel));
    });
    connect(detectBtn, &QPushButton::clicked, this, [this] {
        if (!ollama_) {
            ollama_ = new OllamaDetect(this);
            connect(ollama_, &OllamaDetect::detected, this, [this](const QStringList& models) {
                modelEdit_->setText(models.value(0));
                llmResult_->setText(QStringLiteral("检测到本地模型：") + models.join(QStringLiteral(", ")));
            });
            connect(ollama_, &OllamaDetect::failed, this,
                    [this](const QString& e) { llmResult_->setText(e); });
        }
        ollama_->detect();
    });
    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::testLlmConnection);
    connect(keyEdit_, &QLineEdit::textEdited, this, [this] { keyChanged_ = true; });

    auto* v = qobject_cast<QVBoxLayout*>(layout());
    v->addWidget(box);
}

void SettingsDialog::buildAnalysisGroup() {
    auto* box = new QGroupBox(QStringLiteral("形势分析强度"), this);
    auto* form = new QFormLayout(box);

    analysisCombo_ = new QComboBox(box);
    analysisCombo_->addItem(QStringLiteral("一般（职业7段）"), kAnalysisLevelNormal);
    analysisCombo_->addItem(QStringLiteral("更强（职业8段）"), kAnalysisLevelStronger);
    analysisCombo_->addItem(QStringLiteral("完全正确（职业9段）"), kAnalysisLevelFull);

    analysisHint_ = new QLabel(box);
    analysisHint_->setWordWrap(true);

    form->addRow(QStringLiteral("强度"), analysisCombo_);
    form->addRow(QStringLiteral("说明"), analysisHint_);

    auto updateHint = [this] {
        switch (analysisCombo_->currentData().toInt()) {
            case kAnalysisLevelStronger:
                analysisHint_->setText(QStringLiteral("你可以相信职业八段的判断"));
                break;
            case kAnalysisLevelFull:
                analysisHint_->setText(QStringLiteral(
                    "我不认为柯洁能下赢这个模型。警告：这将会导致形势判断特别特别特别慢，"
                    "也许你可以宣布打挂。"));
                break;
            default:
                analysisHint_->setText(QStringLiteral("你可以相信职业七段的判断"));
                break;
        }
    };
    connect(analysisCombo_, &QComboBox::currentIndexChanged, this, updateHint);

    // 载入当前设置（默认「一般」= 职业7段）。
    const int cur = ModelManager::instance().analysisLevel();
    const int idx = analysisCombo_->findData(cur);
    analysisCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    updateHint();

    auto* v = qobject_cast<QVBoxLayout*>(layout());
    v->addWidget(box);
}

void SettingsDialog::refreshKataGoStatus() {
    const KataGoSettings& s = ModelManager::instance().katago();
    katagoStatus_->setText(ModelManager::instance().katagoStatusText());
    if (!s.variant.isEmpty()) {
        const int idx = variantCombo_->findData(s.variant);
        if (idx >= 0) variantCombo_->setCurrentIndex(idx);
    }
}

void SettingsDialog::refreshLlmFields() {
    const LlmSettings& s = ModelManager::instance().llm();
    const int idx = providerCombo_->findData(s.provider.isEmpty() ? QStringLiteral("custom")
                                                                 : s.provider);
    providerCombo_->setCurrentIndex(idx >= 0 ? idx : providerCombo_->findData(QStringLiteral("custom")));
    if (!s.baseUrl.isEmpty()) baseUrlEdit_->setText(s.baseUrl);
    if (!s.model.isEmpty()) modelEdit_->setText(s.model);
    keyEdit_->setText(KeyStore::mask(ModelManager::instance().apiKey()));
    keyChanged_ = false;
}

void SettingsDialog::startInstall(const QString& variant, const QString& modelId) {
    if (installing_) return;
    installing_ = true;
    installBtn_->setEnabled(false);
    progress_->setVisible(true);
    progress_->setValue(0);

    if (!provisioner_) {
        provisioner_ = new KataGoProvisioner(this);
        connect(provisioner_, &KataGoProvisioner::stepChanged, this,
                [this](const QString& s) { katagoStatus_->setText(s); });
        connect(provisioner_, &KataGoProvisioner::progress, this,
                [this](qint64 r, qint64 t) {
                    progress_->setRange(0, t > 0 ? static_cast<int>(t) : 0);
                    progress_->setValue(static_cast<int>(r));
                });
        connect(provisioner_, &KataGoProvisioner::finished, this,
                [this](bool ok, const QString& e) {
                    installing_ = false;
                    installBtn_->setEnabled(true);
                    progress_->setVisible(false);
                    refreshKataGoStatus();
                    emit settingsChanged();
                    if (!ok && !e.isEmpty()) katagoStatus_->setText(QStringLiteral("失败：") + e);
                });
    }
    provisioner_->install(variant, modelId);
}

void SettingsDialog::testLlmConnection() {
    const QString baseUrl = ensureChatCompletions(baseUrlEdit_->text().trimmed());
    if (baseUrl.isEmpty()) {
        llmResult_->setText(QStringLiteral("请先填写 Base URL"));
        return;
    }
    llmResult_->setText(QStringLiteral("测试中…"));

    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = "只回复 OK";
    QJsonObject body;
    body["model"] = modelEdit_->text().trimmed();
    body["messages"] = QJsonArray{msg};
    body["stream"] = false;

    QNetworkRequest req{QUrl(baseUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QString key = keyEdit_->text().trimmed();
    if (!key.isEmpty() && !key.startsWith(QStringLiteral("••••")))
        req.setRawHeader("Authorization", ("Bearer " + key).toUtf8());

    testReply_ = nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(testReply_, &QNetworkReply::finished, this, [this]() {
        QString out;
        if (testReply_->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(testReply_->readAll());
            const QString content = doc.object()["choices"].toArray().first().toObject()["message"]
                                        .toObject()["content"].toString();
            out = QStringLiteral("成功：") + (content.isEmpty() ? QStringLiteral("(空回复)") : content);
        } else if (testReply_->error() == QNetworkReply::AuthenticationRequiredError) {
            out = QStringLiteral("401：API Key 错误或未授权");
        } else if (testReply_->error() == QNetworkReply::TimeoutError) {
            out = QStringLiteral("超时");
        } else {
            out = QStringLiteral("失败：") + testReply_->errorString();
        }
        testReply_->deleteLater();
        testReply_ = nullptr;
        llmResult_->setText(out);
    });
}

void SettingsDialog::accept() {
    // 保存 LLM 设置
    LlmSettings s = ModelManager::instance().llm();
    s.provider = providerCombo_->currentData().toString();
    s.baseUrl = baseUrlEdit_->text().trimmed();
    s.model = modelEdit_->text().trimmed();
    ModelManager::instance().setLlm(s);
    if (keyChanged_) ModelManager::instance().setApiKey(keyEdit_->text().trimmed());
    ModelManager::instance().setAnalysisLevel(analysisCombo_->currentData().toInt());
    ModelManager::instance().setOnboardingDone(true);
    emit settingsChanged();
    QDialog::accept();
}
