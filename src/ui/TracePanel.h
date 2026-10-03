#pragma once

#include <QDialog>

class QPlainTextEdit;

namespace vgo {
class TraceStore;
}

namespace vgo {

// D5.6：本地面板，把 TraceStore 里最近的 trace 记录按行列出（position hash / route /
// model / latency / verify），并可一键清空。只读展示，不参与任何模型编排。
class TracePanel : public QDialog {
    Q_OBJECT
public:
    explicit TracePanel(TraceStore* store, QWidget* parent = nullptr);

    void refresh();

private:
    TraceStore* store_ = nullptr;
    QPlainTextEdit* view_ = nullptr;
};

}  // namespace vgo
