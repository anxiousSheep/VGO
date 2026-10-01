#include "kata_engine.h"

#include <utility>

#include "ai/gtp.h"

namespace vgo {

KataGoEngine::KataGoEngine(const QString& binary, const QString& model, const QString& config,
                           QObject* parent)
    : QObject(parent), binary_(binary), model_(model), config_(config) {
    start();
}

KataGoEngine::~KataGoEngine() {
    if (proc_) {
        proc_->write("quit\n");
        proc_->closeWriteChannel();
        if (!proc_->waitForFinished(2000)) proc_->kill();
        delete proc_;
    }
}

void KataGoEngine::start() {
    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(proc_, &QProcess::readyReadStandardOutput, this, &KataGoEngine::onStdout);
    connect(proc_, &QProcess::errorOccurred, this, &KataGoEngine::onErrorOccurred);

    QStringList args;
    args << QStringLiteral("gtp") << QStringLiteral("-model") << model_
         << QStringLiteral("-config") << config_;
    proc_->start(binary_, args);
    if (!proc_->waitForStarted(10000)) {
        error_ = QStringLiteral("KataGo 启动失败：%1").arg(proc_->errorString());
        available_ = false;
        return;
    }
    available_ = true;
}

void KataGoEngine::requestMove(const Board& board, Color to_move,
                               const StrengthParams& params, Callback cb) {
    if (!available_) {
        EngineResult r;
        r.move.is_pass = true;
        cb(r);
        return;
    }
    pending_ = std::move(cb);
    size_ = board.size();

    QByteArray cmd;
    cmd += "clear_board\n";
    for (int y = 0; y < board.size(); ++y) {
        for (int x = 0; x < board.size(); ++x) {
            const Color c = board.at(x, y);
            if (c == Color::Empty) continue;
            cmd += (c == Color::Black) ? "play B " : "play W ";
            cmd += gtpPoint(x, y, board.size()).c_str();
            cmd += "\n";
        }
    }
    // Strength knobs (best effort; humanSLProfile may need to live in the
    // config file rather than being settable at runtime).
    cmd += "kata-set-param maxVisits ";
    cmd += QByteArray::number(params.max_visits);
    cmd += "\nkata-set-param temperature ";
    cmd += QByteArray::number(params.temperature);
    cmd += "\nkata-genmove ";
    cmd += (to_move == Color::Black) ? "B\n" : "W\n";
    proc_->write(cmd);
}

void KataGoEngine::cancel() {
    if (pending_) {
        proc_->write("stop\n");
        pending_ = nullptr;
    }
}

void KataGoEngine::onStdout() {
    buffer_ += proc_->readAllStandardOutput();
    int nl;
    while ((nl = buffer_.indexOf('\n')) >= 0) {
        QByteArray line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);
        if (line.isEmpty() || line.startsWith("info ")) continue;  // skip ack/info

        if (line.startsWith("= ")) {
            const QByteArray tok = line.mid(2).trimmed();
            if (tok.isEmpty()) continue;  // ack for clear_board/play/set-param
            EngineResult r;
            if (tok == "pass") {
                r.move.is_pass = true;
            } else {
                Point p;
                if (gtpParsePoint(QString::fromLatin1(tok).toStdString(), size_, &p)) {
                    r.move = {p, false};
                } else {
                    r.move.is_pass = true;  // "resign" or malformed -> pass
                }
            }
            if (pending_) {
                Callback cb = std::move(pending_);
                pending_ = nullptr;
                cb(std::move(r));
            }
        } else if (line.startsWith("? ")) {
            EngineResult r;
            r.move.is_pass = true;
            if (pending_) {
                Callback cb = std::move(pending_);
                pending_ = nullptr;
                cb(std::move(r));
            }
        }
    }
}

void KataGoEngine::onErrorOccurred(QProcess::ProcessError) {
    if (available_ && proc_->state() != QProcess::Running) {
        available_ = false;
        error_ = proc_->errorString();
    }
}

}  // namespace vgo
