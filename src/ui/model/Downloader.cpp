#include "Downloader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace vgo {

Downloader::Downloader(QObject* parent) : QObject(parent) {}

void Downloader::download(const QVector<DownloadSource>& sources, const QString& dest) {
    sources_ = sources;
    dest_ = dest;
    partPath_ = dest + QStringLiteral(".part");
    finished_ = false;
    index_ = 0;

    // Reorder so the last-good source is tried first.
    if (!lastGood_.isEmpty()) {
        for (int i = 0; i < sources_.size(); ++i) {
            if (sources_[i].name == lastGood_) {
                sources_.move(i, 0);
                break;
            }
        }
    }

    // Already have a complete file that matches? Skip the download.
    if (QFileInfo::exists(dest_)) {
        const bool ok = sources_.isEmpty() ||
                        sources_[0].sha256.isEmpty() ||
                        fileSha256(dest_) == sources_[0].sha256;
        if (ok) {
            finish(true, QString());
            return;
        }
    }

    tryNext();
}

void Downloader::tryNext() {
    if (finished_) return;
    if (index_ >= sources_.size()) {
        finish(false, QStringLiteral("所有下载源均失败"));
        return;
    }
    startRequest(sources_[index_]);
}

void Downloader::startRequest(const DownloadSource& src) {
    if (file_) {
        file_->close();
        delete file_;
        file_ = nullptr;
    }
    QFile::remove(partPath_);

    file_ = new QFile(partPath_);
    if (!file_->open(QIODevice::WriteOnly)) {
        emit sourceTried(src.name, false);
        ++index_;
        tryNext();
        return;
    }

    QNetworkRequest req(src.url);
    req.setRawHeader("User-Agent", "VGO/1.0");
    reply_ = nam_.get(req);
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit progress(received, total); });
    connect(reply_, &QNetworkReply::readyRead, this, [this]() {
        if (file_ && reply_) file_->write(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::finished, this, &Downloader::onFinished);
}

void Downloader::onFinished() {
    if (!reply_ || finished_) return;
    const bool httpOk = (reply_->error() == QNetworkReply::NoError);
    // Drain any remaining bytes.
    if (file_ && httpOk) file_->write(reply_->readAll());

    const DownloadSource src = sources_[index_];
    reply_->deleteLater();
    reply_ = nullptr;
    if (file_) {
        file_->close();
        delete file_;
        file_ = nullptr;
    }

    bool ok = httpOk;
    if (ok && !src.sha256.isEmpty()) {
        ok = (fileSha256(partPath_) == src.sha256);
    }
    emit sourceTried(src.name, ok);

    if (ok) {
        QFile::remove(dest_);
        if (QFile::rename(partPath_, dest_)) {
            lastGood_ = src.name;
            finish(true, QString());
        } else {
            finish(false, QStringLiteral("写入目标文件失败"));
        }
    } else {
        QFile::remove(partPath_);
        ++index_;
        tryNext();
    }
}

void Downloader::cancel() {
    finished_ = true;
    if (reply_) {
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
    if (file_) {
        file_->close();
        delete file_;
        file_ = nullptr;
    }
    QFile::remove(partPath_);
}

QByteArray Downloader::fileSha256(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return h.result().toHex();
}

void Downloader::finish(bool ok, const QString& error) {
    if (finished_) return;
    finished_ = true;
    emit finished(ok, error);
}

}  // namespace vgo
