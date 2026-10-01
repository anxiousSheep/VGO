#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>

class QNetworkReply;

namespace vgo {

// Probes a local Ollama server (http://localhost:11434) and lists its models.
class OllamaDetect : public QObject {
    Q_OBJECT
public:
    explicit OllamaDetect(QObject* parent = nullptr);

    void detect();

signals:
    void detected(const QStringList& models);
    void failed(const QString& error);

private:
    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
};

}  // namespace vgo
