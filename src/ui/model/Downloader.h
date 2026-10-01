#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVector>

class QFile;
class QNetworkReply;

namespace vgo {

// One downloadable source (official or mirror) for a single asset.
struct DownloadSource {
    QString name;             // e.g. "官方" / "镜像"
    QUrl url;
    QByteArray sha256;        // hex; empty = skip verification
};

// Downloads an asset, trying multiple sources in order (the last-good source
// is preferred on subsequent runs). Writes to `<dest>.part` then atomically
// renames on success+verification. Emits progress and a single finished().
class Downloader : public QObject {
    Q_OBJECT
public:
    explicit Downloader(QObject* parent = nullptr);

    void download(const QVector<DownloadSource>& sources, const QString& dest);
    void cancel();

    QString lastGoodSource() const { return lastGood_; }
    void setLastGoodSource(const QString& name) { lastGood_ = name; }

signals:
    void progress(qint64 received, qint64 total);
    void sourceTried(const QString& name, bool ok);
    void finished(bool ok, const QString& error);

private:
    void tryNext();
    void startRequest(const DownloadSource& src);
    void onFinished();
    static QByteArray fileSha256(const QString& path);
    void finish(bool ok, const QString& error);

    QNetworkAccessManager nam_;
    QVector<DownloadSource> sources_;
    QString dest_;
    QString partPath_;
    QFile* file_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    int index_ = 0;
    QString lastGood_;
    bool finished_ = false;
};

}  // namespace vgo
