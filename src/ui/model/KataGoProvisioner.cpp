#include "KataGoProvisioner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include "Downloader.h"
#include "ModelManager.h"
#include "ModelsManifest.h"

namespace vgo {

KataGoProvisioner::KataGoProvisioner(QObject* parent) : QObject(parent) {}

KataGoProvisioner::~KataGoProvisioner() {
    cancel();
}

QString KataGoProvisioner::dataDir() const {
    return ModelManager::instance().dataDir();
}

QString KataGoProvisioner::katagoDir() const {
    return dataDir() + QStringLiteral("/katago");
}

QString KataGoProvisioner::binaryZipPath() const {
    return dataDir() + QStringLiteral("/katago.zip");
}

void KataGoProvisioner::install(const QString& variant, const QString& modelId) {
    variant_ = variant;
    modelId_ = modelId;
    cancelled_ = false;

    if (variant_.isEmpty()) {
        const GpuKind gpu = detectGpuKind();
        variant_ = (gpu == GpuKind::OpenCl) ? QStringLiteral("opencl") : QStringLiteral("eigen");
    }

    emit stepChanged(QStringLiteral("准备下载 KataGo（") + variant_ + QStringLiteral("）…"));
    downloadBinary();
}

void KataGoProvisioner::downloadBinary() {
    const auto assets = katagoAssets();
    QVector<DownloadSource> sources;
    for (const auto& a : assets) {
        if (QString::fromStdString(a.variant) != variant_) continue;
        for (const auto& murl : a.mirrors) {
            DownloadSource m;
            m.name = QStringLiteral("镜像");
            m.url = QString::fromStdString(murl);
            sources.push_back(m);
        }
        DownloadSource official;
        official.name = QStringLiteral("官方");
        official.url = QString::fromStdString(a.officialUrl);
        sources.push_back(official);
        break;
    }
    if (sources.isEmpty()) {
        finish(false, QStringLiteral("未知的变体：") + variant_);
        return;
    }

    downloader_ = new Downloader(this);
    connect(downloader_, &Downloader::progress, this, &KataGoProvisioner::progress);
    connect(downloader_, &Downloader::finished, this, &KataGoProvisioner::onBinaryDone);
    downloader_->download(sources, binaryZipPath());
}

void KataGoProvisioner::onBinaryDone(bool ok, const QString& error) {
    if (cancelled_) return;
    if (!ok) {
        finish(false, QStringLiteral("下载 KataGo 失败：") + error);
        return;
    }
    extractBinary();
}

void KataGoProvisioner::extractBinary() {
    emit stepChanged(QStringLiteral("正在解压 KataGo…"));
    QDir().mkpath(katagoDir());

    extractProc_ = new QProcess(this);
    connect(extractProc_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                extractProc_->deleteLater();
                extractProc_ = nullptr;
                if (cancelled_) return;
                if (code != 0) {
                    finish(false, QStringLiteral("解压失败"));
                    return;
                }
                QFile::remove(binaryZipPath());
                writeConfig();
            });

    const QString ps = QStringLiteral(
        "powershell.exe -NoProfile -Command \"Expand-Archive -Path '%1' -DestinationPath '%2' -Force\"")
                           .arg(binaryZipPath(), katagoDir());
    // 注意：Qt6 必须用 startCommand（把整串当命令行），start(QString) 会把整串当程序名。
    extractProc_->startCommand(ps);
}

void KataGoProvisioner::writeConfig() {
    QDir().mkpath(dataDir() + QStringLiteral("/models"));

    KataGoSettings s = ModelManager::instance().katago();
    s.enabled = true;
    s.binary = katagoDir() + QStringLiteral("/katago.exe");
    // 用 release 自带的默认 GTP 配置（解压后一定存在），不手写最小配置。
    s.config = katagoDir() + QStringLiteral("/default_gtp.cfg");
    s.version = QString::fromStdString(kataGoVersion());
    s.variant = variant_;
    ModelManager::instance().setKatago(s);

    if (modelId_.isEmpty()) {
        finish(true, QString());
        return;
    }
    downloadModel();
}

void KataGoProvisioner::downloadModel() {
    const ModelAsset* m = findModel(modelId_.toStdString());
    if (!m) {
        finish(false, QStringLiteral("未知的模型：") + modelId_);
        return;
    }
    emit stepChanged(QStringLiteral("正在下载模型 ") + QString::fromStdString(m->name) +
                     QStringLiteral("…"));

    const QString dest = dataDir() + QStringLiteral("/models/") + modelId_ + QStringLiteral(".bin.gz");
    if (QFileInfo::exists(dest)) {
        // Model already present; record it and finish.
        KataGoSettings s = ModelManager::instance().katago();
        if (m->humanStyle) s.humanModel = dest; else s.model = dest;
        ModelManager::instance().setKatago(s);
        finish(true, QString());
        return;
    }

    QVector<DownloadSource> sources;
    for (const auto& murl : m->mirrors) {
        DownloadSource mir;
        mir.name = QStringLiteral("镜像");
        mir.url = QString::fromStdString(murl);
        sources.push_back(mir);
    }
    DownloadSource official;
    official.name = QStringLiteral("官方");
    official.url = QString::fromStdString(m->officialUrl);
    sources.push_back(official);

    downloader_ = new Downloader(this);
    connect(downloader_, &Downloader::progress, this, &KataGoProvisioner::progress);
    connect(downloader_, &Downloader::finished, this, &KataGoProvisioner::onModelDone);
    downloader_->download(sources, dest);
}

void KataGoProvisioner::onModelDone(bool ok, const QString& error) {
    if (cancelled_) return;
    if (!ok) {
        finish(false, QStringLiteral("下载模型失败：") + error);
        return;
    }
    const ModelAsset* m = findModel(modelId_.toStdString());
    const QString dest = dataDir() + QStringLiteral("/models/") + modelId_ + QStringLiteral(".bin.gz");
    KataGoSettings s = ModelManager::instance().katago();
    if (m && m->humanStyle) s.humanModel = dest; else s.model = dest;
    ModelManager::instance().setKatago(s);
    finish(true, QString());
}

void KataGoProvisioner::cancel() {
    cancelled_ = true;
    if (downloader_) {
        downloader_->cancel();
        downloader_->deleteLater();
        downloader_ = nullptr;
    }
    if (extractProc_) {
        extractProc_->kill();
        extractProc_->deleteLater();
        extractProc_ = nullptr;
    }
}

void KataGoProvisioner::finish(bool ok, const QString& error) {
    if (cancelled_) return;
    cancelled_ = true;  // 防止重复
    emit finished(ok, error);
}

}  // namespace vgo
