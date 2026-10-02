#include "VariationPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace vgo {

VariationPanel::VariationPanel(QWidget* parent) : QWidget(parent) {
    stateLabel_ = new QLabel(QStringLiteral("试下模式"), this);

    aiBtn_ = new QPushButton(QStringLiteral("AI 演示一手"), this);
    undoBtn_ = new QPushButton(QStringLiteral("回退"), this);
    exitBtn_ = new QPushButton(QStringLiteral("退出演示"), this);

    connect(aiBtn_, &QPushButton::clicked, this, &VariationPanel::aiMoveRequested);
    connect(undoBtn_, &QPushButton::clicked, this, &VariationPanel::undoRequested);
    connect(exitBtn_, &QPushButton::clicked, this, &VariationPanel::exitRequested);

    auto* lay = new QHBoxLayout(this);
    lay->addWidget(stateLabel_);
    lay->addSpacing(8);
    lay->addWidget(aiBtn_);
    lay->addWidget(undoBtn_);
    lay->addStretch(1);
    lay->addWidget(exitBtn_);

    setCanUndo(false);
}

void VariationPanel::setCanUndo(bool can) {
    undoBtn_->setEnabled(can);
}

}  // namespace vgo
