#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;

namespace vgo {
class KataGoProvisioner;
class OllamaDetect;
}

// Model/settings dialog: KataGo install & model selection on the left, LLM
// provider/key/test on the right. Writes through vgo::ModelManager.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

signals:
    void settingsChanged();

private:
    void buildKataGoGroup();
    void buildLlmGroup();
    void buildAnalysisGroup();
    void refreshKataGoStatus();
    void refreshLlmFields();
    void startInstall(const QString& variant, const QString& modelId);
    void testLlmConnection();
    void accept() override;

    // KataGo
    QLabel* katagoStatus_ = nullptr;
    QComboBox* variantCombo_ = nullptr;
    QPushButton* installBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* modelButtons_[3] = {nullptr, nullptr, nullptr};

    // LLM
    QComboBox* providerCombo_ = nullptr;
    QLineEdit* baseUrlEdit_ = nullptr;
    QLineEdit* modelEdit_ = nullptr;
    QLineEdit* keyEdit_ = nullptr;
    QLabel* llmResult_ = nullptr;

    // 形势分析强度
    QComboBox* analysisCombo_ = nullptr;
    QLabel* analysisHint_ = nullptr;

    vgo::KataGoProvisioner* provisioner_ = nullptr;
    vgo::OllamaDetect* ollama_ = nullptr;
    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* testReply_ = nullptr;
    bool keyChanged_ = false;
    bool installing_ = false;
};
