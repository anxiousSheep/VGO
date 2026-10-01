#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

#include "ai/engine.h"

namespace vgo {

// KataGo GTP client. Owns a persistent `katago gtp` subprocess and answers
// requestMove by rebuilding the position and issuing `kata-genmove`.
//
// NOTE: the protocol handling follows KataGo's GTP extensions but has not been
// verified end-to-end against a live KataGo install. Callers should fall back
// to FallbackEngine when available() is false.
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
};

}  // namespace vgo
