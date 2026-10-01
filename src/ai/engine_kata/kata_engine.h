#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

#include "ai/engine.h"

namespace vgo {

// KataGo GTP client. Owns a persistent `katago gtp` subprocess and answers
// requestMove by rebuilding the position and issuing `kata-genmove_analyze`
// (move + winrate/scoreLead/candidates/pv), which feeds the Analysis struct.
class KataGoEngine : public QObject, public EngineInterface {
    Q_OBJECT
public:
    KataGoEngine(const QString& binary, const QString& model, const QString& config,
                 QObject* parent = nullptr);
    ~KataGoEngine() override;

    bool available() const { return available_; }
    QString errorString() const { return error_; }

    void requestMove(const Board& board, Color to_move,
                     const StrengthParams& params, Callback cb) override;
    void cancel() override;

signals:
    // 进程状态提示（如首次 OpenCL 调优），供 UI 显示。
    void statusMessage(const QString& msg);

private:
    void start();
    void onStdout();
    void onErrorOccurred(QProcess::ProcessError);

    QString binary_;
    QString model_;
    QString config_;
    QProcess* proc_ = nullptr;
    bool available_ = false;
    QString error_;
    Callback pending_;
    QByteArray buffer_;
    int size_ = 0;  // board size of the in-flight request (for parsing moves)
    std::vector<MoveCandidate> pending_candidates_;  // 正在收的 kata-genmove_analyze 候选
};

}  // namespace vgo
