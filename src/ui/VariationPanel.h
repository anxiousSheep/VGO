#pragma once

#include <QWidget>

#include "ai/variation.h"

class QLabel;
class QPushButton;

namespace vgo {

// Board-demo control strip (N2.3): shown only in demo mode. Lets the user step
// through a variation with "权威 AI 后续", "确认下一步", "上一步", "重放" and
// "退出演示". Purely a view — it emits requests, the MainWindow drives the
// VariationController state machine.
class VariationPanel : public QWidget {
    Q_OBJECT
public:
    explicit VariationPanel(QWidget* parent = nullptr);

    void setState(VariationState s);

signals:
    void aiMoveRequested();
    void confirmRequested();
    void backRequested();
    void replayRequested();
    void exitRequested();

private:
    QLabel* stateLabel_ = nullptr;
    QPushButton* aiBtn_ = nullptr;
    QPushButton* confirmBtn_ = nullptr;
    QPushButton* backBtn_ = nullptr;
    QPushButton* replayBtn_ = nullptr;
    QPushButton* exitBtn_ = nullptr;
};

}  // namespace vgo
