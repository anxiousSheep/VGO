#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace vgo {

// Board-demo control strip (N2): shown only in 试下 (try-out) mode. The user
// plays/undoes moves freely; there is no per-step confirmation. Buttons: "AI
// 演示一手" (authoritative continuation), "回退" (undo) and "退出演示".
class VariationPanel : public QWidget {
    Q_OBJECT
public:
    explicit VariationPanel(QWidget* parent = nullptr);

    void setCanUndo(bool can);

signals:
    void aiMoveRequested();
    void undoRequested();
    void exitRequested();

private:
    QLabel* stateLabel_ = nullptr;
    QPushButton* aiBtn_ = nullptr;
    QPushButton* undoBtn_ = nullptr;
    QPushButton* exitBtn_ = nullptr;
};

}  // namespace vgo
