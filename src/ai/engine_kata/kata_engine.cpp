#include "kata_engine.h"

#include <QFileInfo>
#include <QList>
#include <algorithm>
#include <utility>

#include "ai/gtp.h"

namespace vgo {

namespace {

// kata-genmove_analyze 需要足够的 visits 才会输出 info move 分析（胜率/目数/候选）。
// 1 visit 时不输出，导致分析全为 0。此处给分析一个下限。
constexpr int kMinAnalysisVisits = 20;

// 解析 `info move E5 visits 7 ... winrate 0.1 ... scoreLead -0.2 ... order 0 pv E5 E7`。
// 返回候选；isSymmetryOf（对称重复）或坐标非法时返回 p.x<0（调用方跳过）。
MoveCandidate parseCandidate(const QByteArray& line, int size) {
    MoveCandidate c;
    const QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 3) return c;
    Point p;
    if (!gtpParsePoint(QString::fromLatin1(parts[2]).toStdString(), size, &p)) return c;
    c.p = p;
    for (int i = 3; i < parts.size(); ++i) {
        const QByteArray key = parts[i];
        if (key == "winrate" && i + 1 < parts.size()) {
            c.winrate = parts[++i].toDouble();
        } else if (key == "scoreLead" && i + 1 < parts.size()) {
            c.score_lead = parts[++i].toDouble();
        } else if (key == "visits" && i + 1 < parts.size()) {
            c.visits = parts[++i].toInt();
        } else if (key == "isSymmetryOf") {
            c.p = {-1, -1};  // 对称重复，跳过
            return c;
        } else if (key == "pv") {
            for (int j = i + 1; j < parts.size(); ++j) {
                Point pp;
                if (gtpParsePoint(QString::fromLatin1(parts[j]).toStdString(), size, &pp)) {
                    c.pv.push_back(pp);
                }
            }
            break;  // pv 是最后一个字段
        }
    }
    return c;
}

}  // namespace

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
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        // KataGo 首次运行会在 stderr 输出 OpenCL 自动调优进度。
        const QByteArray err = proc_->readAllStandardError();
        if (err.contains("autotuning") || err.contains("Tuning")) {
            emit statusMessage(
                QStringLiteral("KataGo 首次运行正在 GPU 调优（约几分钟），完成后即可落子"));
        }
    });

    QStringList args;
    args << QStringLiteral("gtp") << QStringLiteral("-model") << model_;
    if (!config_.isEmpty()) args << QStringLiteral("-config") << config_;
    // 固定工作目录到 kata 二进制所在目录，使 OpenCL 调优缓存（KataGoData/）
    // 落盘位置稳定，首次调优后无需重复。
    proc_->setWorkingDirectory(QFileInfo(binary_).absolutePath());
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
    // 必须先告知棋盘大小，否则 KataGo 默认 19×19，返回的坐标无法在 9/13 路解析。
    cmd += "boardsize ";
    cmd += QByteArray::number(board.size());
    cmd += "\nclear_board\n";
    for (int y = 0; y < board.size(); ++y) {
        for (int x = 0; x < board.size(); ++x) {
            const Color c = board.at(x, y);
            if (c == Color::Empty) continue;
            cmd += (c == Color::Black) ? "play B " : "play W ";
            cmd += gtpPoint(x, y, board.size()).c_str();
            cmd += "\n";
        }
    }
    // 强度：仅 maxVisits 可经 kata-set-param 运行时覆盖。
    // 用 kata-genmove_analyze 一次拿回落子 + 胜率/目数差/候选/pv，供 LLM Grounding。
    cmd += "kata-set-param maxVisits ";
    cmd += QByteArray::number(std::max(params.max_visits, kMinAnalysisVisits));
    cmd += "\nkata-genmove_analyze ";
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

        if (line.isEmpty() || line == "=" ||
            (line.startsWith("info ") && !line.startsWith("info move "))) {
            continue;  // ack 或其它 info
        }

        if (line.startsWith("info move ")) {
            MoveCandidate c = parseCandidate(line, size_);
            if (c.p.x >= 0) pending_candidates_.push_back(std::move(c));
            continue;
        }

        if (line.startsWith("play ")) {
            const QByteArray tok = line.mid(5).trimmed();
            EngineResult r;
            Point p;
            if (gtpParsePoint(QString::fromLatin1(tok).toStdString(), size_, &p)) {
                r.move = {p, false};
            } else {
                r.move.is_pass = true;
            }
            if (!pending_candidates_.empty()) {
                const MoveCandidate& best = pending_candidates_.front();
                r.analysis.valid = true;
                r.analysis.winrate = best.winrate;
                r.analysis.score_lead = best.score_lead;
                r.analysis.best = best;
                r.analysis.candidates = std::move(pending_candidates_);
            }
            pending_candidates_.clear();
            if (pending_) {
                Callback cb = std::move(pending_);
                pending_ = nullptr;
                cb(std::move(r));
            }
            continue;
        }

        if (line.startsWith("= ")) {
            // 兜底：单行结果（pass/resign 或旧格式）
            const QByteArray tok = line.mid(2).trimmed();
            if (tok.isEmpty()) continue;
            EngineResult r;
            if (tok == "pass") {
                r.move.is_pass = true;
            } else {
                Point p;
                if (gtpParsePoint(QString::fromLatin1(tok).toStdString(), size_, &p)) {
                    r.move = {p, false};
                } else {
                    r.move.is_pass = true;
                }
            }
            pending_candidates_.clear();
            if (pending_) {
                Callback cb = std::move(pending_);
                pending_ = nullptr;
                cb(std::move(r));
            }
            continue;
        }

        if (line.startsWith("? ")) {
            EngineResult r;
            r.move.is_pass = true;
            pending_candidates_.clear();
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
