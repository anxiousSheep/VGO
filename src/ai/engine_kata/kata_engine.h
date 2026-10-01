#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

#include "ai/engine.h"

namespace vgo {

// Parse the space-separated text returned by KataGo's `kata-search_analyze`
// command (`info move ...` candidates + `rootInfo winrate/scoreLead` + `ownership
// <size*size floats>`) into an Analysis. `size` is the board size, used to decode
// move strings and size the ownership map. Exposed as a free function so it can
// be unit-tested without a KataGo process.
bool parseKataAnalysis(const QByteArray& text, int size, Analysis* out);

// KataGo GTP client. Owns a persistent `katago gtp` subprocess and answers
// requestMove by rebuilding the position and issuing `kata-genmove_analyze`
// (move + winrate/scoreLead/candidates/pv), and analyze by issuing
// `kata-search_analyze` (winrate/scoreLead + ownership), both feeding Analysis.
class KataGoEngine : public QObject, public EngineInterface {
    Q_OBJECT
public:
    KataGoEngine(const QString& binary, const QString& model, const QString& config,
                 const QString& human_profile = QString(), QObject* parent = nullptr);
    ~KataGoEngine() override;

    bool available() const { return available_; }
    QString errorString() const { return error_; }

    void requestMove(const Board& board, Color to_move,
                     const StrengthParams& params, Callback cb) override;
    void analyze(const Board& board, Color to_move,
                 const StrengthParams& params, AnalysisCallback cb) override;
    void cancel() override;

signals:
    // 进程状态提示（如首次 OpenCL 调优），供 UI 显示。
    void statusMessage(const QString& msg);

private:
    enum class PendingKind { None, Move, Analyze };

    void start();
    void onStdout();
    void handleAnalyzeLine(const QByteArray& line);
    void onErrorOccurred(QProcess::ProcessError);
    bool busy() const { return pending_ != nullptr || pending_analysis_ != nullptr; }

    QString binary_;
    QString model_;
    QString config_;
    QString human_profile_;  // HumanSL 档位（如 rank_1d），空 = 标准模型
    QProcess* proc_ = nullptr;
    bool available_ = false;
    QString error_;
    PendingKind pending_kind_ = PendingKind::None;
    Callback pending_;
    AnalysisCallback pending_analysis_;
    QByteArray buffer_;
    QByteArray analysis_buffer_;  // 累积 kata-search_analyze 的分析文本（可能跨行）
    int size_ = 0;  // board size of the in-flight request (for parsing moves)
    std::vector<MoveCandidate> pending_candidates_;  // 正在收的 kata-genmove_analyze 候选
};

}  // namespace vgo
