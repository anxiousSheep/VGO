#pragma once

#include <QObject>
#include <QString>

#include "GpuDetect.h"

class QProcess;

namespace vgo {
class Downloader;

// One-click KataGo install: pick variant -> download binary -> extract -> write
// a minimal gtp.cfg -> (optional) download a network model -> record in
// ModelManager. Mirrors the settings dialog's install/update action.
class KataGoProvisioner : public QObject {
    Q_OBJECT
public:
    explicit KataGoProvisioner(QObject* parent = nullptr);
    ~KataGoProvisioner() override;

    // modelId empty => binary only. `variant` empty => auto-detect from `gpu`.
    void install(const QString& variant, const QString& modelId);
    void cancel();

signals:
    void stepChanged(const QString& step);
    void progress(qint64 received, qint64 total);
    void finished(bool ok, const QString& error);

private:
    void downloadBinary();
    void onBinaryDone(bool ok, const QString& error);
    void extractBinary();
    void writeConfig();
    void downloadModel();
    void onModelDone(bool ok, const QString& error);
    void finish(bool ok, const QString& error);

    QString dataDir() const;
    QString binaryZipPath() const;
    QString katagoDir() const;

    QString variant_;
    QString modelId_;
    Downloader* downloader_ = nullptr;
    QProcess* extractProc_ = nullptr;
    bool cancelled_ = false;
};

}  // namespace vgo
