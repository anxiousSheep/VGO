#include "kata_engine.h"

#include <QFileInfo>
#include <QList>
#include <algorithm>
#include <cmath>
#include <utility>

#include "ai/gtp.h"

namespace vgo {

namespace {

// kata-genmove_analyze 需要足够的 visits 才会输出 info move 分析（胜率/目数/候选）。
// 1 visit 时不输出，导致分析全为 0。此处给分析一个下限。
constexpr int kMinAnalysisVisits = 20;

// 形势评估（kata-search_analyze）用固定小预算，保证手动触发快速返回，
// 不随段位 maxVisits（职业高段可达 100k）膨胀。
constexpr int kAnalyzeMaxVisits = 200;

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

// Whether a token is a known scalar-valued key (followed by exactly one value).
bool isScalarKey(const QByteArray& k) {
    static const char* keys[] = {
        "visits",       "edgeVisits", "utility",    "winrate",    "scoreMean",
        "scoreStdev",   "scoreLead",  "scoreSelfplay", "prior",   "noResultValue",
        "lcb",          "utilityLcb", "weight",     "order",      "pvVisits",
        "pvEdgeVisits", "rawStWrError", "rawVarTimeLeft", "stWrError", "varTimeLeft",
    };
    for (const char* s : keys)
        if (k == s) return true;
    return false;
}

// Whether a token starts a new top-level section of the analysis text.
bool isSectionStart(const QByteArray& k) {
    return k == "info" || k == "rootInfo" || k == "ownership" || k == "play";
}

}  // namespace

// Parses the space-separated analysis text from `kata-search_analyze`, of the
// form:
//   info move E5 visits 467 ... winrate 0.11 ... scoreLead -0.74 ... order 0 pv E5 G5 ...
//   info move F5 ... rootInfo visits 505 winrate 0.108 scoreLead -0.726 ...
//   ownership <size*size floats> ... play E5
// Tokens may span newlines, so callers concatenate lines with spaces first.
bool parseKataAnalysis(const QByteArray& text, int size, Analysis* out) {
    if (!out) return false;
    const QList<QByteArray> toks = text.split(' ');
    const int n = toks.size();
    int i = 0;

    auto parsePoint = [&](const QByteArray& t, Point* p) {
        return gtpParsePoint(QString::fromLatin1(t).toStdString(), size, p);
    };

    MoveCandidate* cur = nullptr;
    bool have_root = false;
    while (i < n) {
        const QByteArray t = toks[i];
        if (t.isEmpty()) { ++i; continue; }

        if (t == "info") {
            if (i + 2 >= n || toks[i + 1] != "move") { ++i; continue; }
            out->candidates.push_back(MoveCandidate{});
            cur = &out->candidates.back();
            Point p;
            if (parsePoint(toks[i + 2], &p)) cur->p = p;
            else cur->is_pass = true;
            i += 3;
            while (i < n && !isSectionStart(toks[i])) {
                const QByteArray k = toks[i];
                if (k == "visits" && i + 1 < n) { cur->visits = toks[i + 1].toInt(); i += 2; }
                else if (k == "winrate" && i + 1 < n) { cur->winrate = toks[i + 1].toDouble(); i += 2; }
                else if (k == "scoreLead" && i + 1 < n) { cur->score_lead = toks[i + 1].toDouble(); i += 2; }
                else if (k == "isSymmetryOf") { cur->p = {-1, -1}; i += (i + 1 < n ? 2 : 1); }
                else if (k == "pv") {
                    ++i;
                    while (i < n && !isSectionStart(toks[i])) {
                        Point pp;
                        if (parsePoint(toks[i], &pp)) { cur->pv.push_back(pp); ++i; }
                        else { ++i; }  // skip non-move token
                    }
                } else if (isScalarKey(k)) {
                    i += (i + 1 < n ? 2 : 1);
                } else {
                    ++i;  // unknown token, skip
                }
            }
            continue;
        }

        if (t == "rootInfo") {
            have_root = true;
            ++i;
            while (i < n && !isSectionStart(toks[i])) {
                const QByteArray k = toks[i];
                if (k == "winrate" && i + 1 < n) { out->winrate = toks[i + 1].toDouble(); i += 2; }
                else if (k == "scoreLead" && i + 1 < n) { out->score_lead = toks[i + 1].toDouble(); i += 2; }
                else if (isScalarKey(k)) { i += (i + 1 < n ? 2 : 1); }
                else { ++i; }
            }
            continue;
        }

        if (t == "ownership") {
            out->ownership.size = size;
            out->ownership.value.assign(static_cast<std::size_t>(size) * size, 0.0);
            ++i;
            int idx = 0;
            while (i < n && idx < size * size) {
                const QByteArray v = toks[i];
                if (!v.isEmpty()) {
                    bool ok = false;
                    const double d = v.toDouble(&ok);
                    if (ok) { out->ownership.value[static_cast<std::size_t>(idx)] = d; ++idx; }
                }
                ++i;
            }
            continue;
        }

        if (t == "play") break;  // completion marker (move reported, not committed)
        ++i;
    }

    // Drop symmetry duplicates (p.x < 0, not pass), mirroring the genmove path.
    out->candidates.erase(
        std::remove_if(out->candidates.begin(), out->candidates.end(),
                       [](const MoveCandidate& c) { return c.p.x < 0 && !c.is_pass; }),
        out->candidates.end());
    if (!out->candidates.empty()) out->best = out->candidates.front();

    out->valid = have_root || !out->candidates.empty() || !out->ownership.empty();
    return out->valid;
}

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
    if (!available_ || busy()) {
        EngineResult r;
        r.move.is_pass = true;
        cb(r);
        return;
    }
    pending_ = std::move(cb);
    pending_kind_ = PendingKind::Move;
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

void KataGoEngine::analyze(const Board& board, Color to_move,
                           const StrengthParams&, AnalysisCallback cb) {
    if (!available_ || busy()) {
        cb(Analysis{});
        return;
    }
    pending_analysis_ = std::move(cb);
    pending_kind_ = PendingKind::Analyze;
    analysis_buffer_.clear();
    size_ = board.size();

    QByteArray cmd;
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
    // 形势评估用固定小预算（kAnalyzeMaxVisits），并请求每点归属 ownership 与 rootInfo。
    // 用 kata-search_analyze（不落子）而非 kata-analyze：后者是连续分析命令，在默认
    // 配置下不会一次性回显结果；前者等价于 genmove_analyze 但不改棋盘。
    cmd += "kata-set-param maxVisits ";
    cmd += QByteArray::number(kAnalyzeMaxVisits);
    cmd += "\nkata-search_analyze ";
    cmd += (to_move == Color::Black) ? "B" : "W";
    cmd += " ownership true rootInfo true\n";
    proc_->write(cmd);
}

void KataGoEngine::cancel() {
    if (pending_ || pending_analysis_) {
        proc_->write("stop\n");
        pending_ = nullptr;
        pending_analysis_ = nullptr;
        pending_kind_ = PendingKind::None;
        pending_candidates_.clear();
        analysis_buffer_.clear();
    }
}

void KataGoEngine::onStdout() {
    buffer_ += proc_->readAllStandardOutput();
    int nl;
    while ((nl = buffer_.indexOf('\n')) >= 0) {
        QByteArray line = buffer_.left(nl).trimmed();
        buffer_.remove(0, nl + 1);

        if (pending_kind_ == PendingKind::Analyze) {
            handleAnalyzeLine(line);
            continue;
        }

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
                pending_kind_ = PendingKind::None;
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
                pending_kind_ = PendingKind::None;
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
                pending_kind_ = PendingKind::None;
                cb(std::move(r));
            }
        }
    }
}

void KataGoEngine::handleAnalyzeLine(const QByteArray& line) {
    // Skip empty lines and the GTP ack ("=") that precedes the analysis text.
    if (line.isEmpty() || line == "=") return;

    // Completion: the final "play <move>" line (kata-search_analyze reports the
    // chosen move without committing it), or a GTP error ("? ...").
    if (line.startsWith("play ") || line.startsWith("? ")) {
        Analysis a;
        if (line.startsWith("play ")) parseKataAnalysis(analysis_buffer_, size_, &a);
        // On error, `a` stays invalid (no ownership/candidates) -> UI degrades.
        analysis_buffer_.clear();
        pending_kind_ = PendingKind::None;
        AnalysisCallback cb = std::move(pending_analysis_);
        pending_analysis_ = nullptr;
        if (cb) cb(std::move(a));
        return;
    }

    // Accumulate analysis text (info move ... / rootInfo ... / ownership ...);
    // tokens may span lines, so join with a single space.
    analysis_buffer_ += line;
    analysis_buffer_ += ' ';
}

void KataGoEngine::onErrorOccurred(QProcess::ProcessError) {
    if (available_ && proc_->state() != QProcess::Running) {
        available_ = false;
        error_ = proc_->errorString();
    }
}

}  // namespace vgo
