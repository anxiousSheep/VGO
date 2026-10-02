#include "VariationPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace vgo {

VariationPanel::VariationPanel(QWidget* parent) : QWidget(parent) {
    stateLabel_ = new QLabel(QStringLiteral("演示模式"), this);

    aiBtn_ = new QPushButton(QStringLiteral("AI 演示一手"), this);
    confirmBtn_ = new QPushButton(QStringLiteral("确认下一步"), this);
    backBtn_ = new QPushButton(QStringLiteral("上一步"), this);
    replayBtn_ = new QPushButton(QStringLiteral("重放"), this);
    exitBtn_ = new QPushButton(QStringLiteral("退出演示"), this);

    connect(aiBtn_, &QPushButton::clicked, this, &VariationPanel::aiMoveRequested);
    connect(confirmBtn_, &QPushButton::clicked, this, &VariationPanel::confirmRequested);
    connect(backBtn_, &QPushButton::clicked, this, &VariationPanel::backRequested);
    connect(replayBtn_, &QPushButton::clicked, this, &VariationPanel::replayRequested);
    connect(exitBtn_, &QPushButton::clicked, this, &VariationPanel::exitRequested);

    auto* lay = new QHBoxLayout(this);
    lay->addWidget(stateLabel_);
    lay->addSpacing(8);
    lay->addWidget(aiBtn_);
    lay->addWidget(confirmBtn_);
    lay->addWidget(backBtn_);
    lay->addWidget(replayBtn_);
    lay->addStretch(1);
    lay->addWidget(exitBtn_);

    setState(VariationState::WaitingForMove);
}

void VariationPanel::setState(VariationState s) {
    switch (s) {
        case VariationState::WaitingForMove:
            stateLabel_->setText(QStringLiteral("演示：待落子（点棋盘或「AI 演示一手」）"));
            aiBtn_->setEnabled(true);
            confirmBtn_->setEnabled(false);
            break;
        case VariationState::Explaining:
            stateLabel_->setText(QStringLiteral("演示：讲解中…"));
            aiBtn_->setEnabled(false);
            confirmBtn_->setEnabled(false);
            break;
        case VariationState::WaitingForConfirm:
            stateLabel_->setText(QStringLiteral("演示：讲解完成，请确认下一步"));
            aiBtn_->setEnabled(false);
            confirmBtn_->setEnabled(true);
            break;
        case VariationState::Finished:
        default:
            stateLabel_->setText(QStringLiteral("演示模式"));
            aiBtn_->setEnabled(false);
            confirmBtn_->setEnabled(false);
            break;
    }
    backBtn_->setEnabled(true);
    replayBtn_->setEnabled(true);
    exitBtn_->setEnabled(true);
}

}  // namespace vgo
