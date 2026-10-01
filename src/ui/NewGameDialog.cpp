#include "NewGameDialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QSlider>
#include <QVBoxLayout>

#include "ai/strength.h"

NewGameDialog::NewGameDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("新对局"));

    // 模式
    auto* placementRadio = new QRadioButton(QStringLiteral("摆棋"), this);
    auto* aiRadio = new QRadioButton(QStringLiteral("人机对弈"), this);
    aiRadio->setChecked(true);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->addButton(placementRadio, 0);
    modeGroup_->addButton(aiRadio, 1);

    // 棋盘大小
    auto* size9 = new QRadioButton(QStringLiteral("9×9"), this);
    auto* size13 = new QRadioButton(QStringLiteral("13×13"), this);
    auto* size19 = new QRadioButton(QStringLiteral("19×19"), this);
    size9->setChecked(true);
    sizeGroup_ = new QButtonGroup(this);
    sizeGroup_->addButton(size9, 9);
    sizeGroup_->addButton(size13, 13);
    sizeGroup_->addButton(size19, 19);

    // 执子（仅人机对弈）
    auto* blackRadio = new QRadioButton(QStringLiteral("执黑"), this);
    auto* whiteRadio = new QRadioButton(QStringLiteral("执白"), this);
    auto* nigiriRadio = new QRadioButton(QStringLiteral("猜先"), this);
    blackRadio->setChecked(true);
    colorGroup_ = new QButtonGroup(this);
    colorGroup_->addButton(blackRadio, 0);
    colorGroup_->addButton(whiteRadio, 1);
    colorGroup_->addButton(nigiriRadio, 2);

    // 难度（19 级）
    levelSlider_ = new QSlider(Qt::Horizontal, this);
    levelSlider_->setRange(1, 19);
    levelSlider_->setValue(1);
    levelLabel_ = new QLabel(this);

    auto* modeRow = new QHBoxLayout;
    modeRow->addWidget(placementRadio);
    modeRow->addWidget(aiRadio);
    modeRow->addStretch(1);

    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(size9);
    sizeRow->addWidget(size13);
    sizeRow->addWidget(size19);
    sizeRow->addStretch(1);

    colorRow_ = new QWidget(this);
    auto* colorLayout = new QHBoxLayout(colorRow_);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->addWidget(blackRadio);
    colorLayout->addWidget(whiteRadio);
    colorLayout->addWidget(nigiriRadio);
    colorLayout->addStretch(1);

    levelRow_ = new QWidget(this);
    auto* levelLayout = new QHBoxLayout(levelRow_);
    levelLayout->setContentsMargins(0, 0, 0, 0);
    levelLayout->addWidget(levelSlider_, 1);
    levelLayout->addWidget(levelLabel_);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("模式"), modeRow);
    form->addRow(QStringLiteral("棋盘大小"), sizeRow);
    form->addRow(QStringLiteral("执子"), colorRow_);
    form->addRow(QStringLiteral("难度"), levelRow_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &NewGameDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewGameDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(modeGroup_, &QButtonGroup::idToggled, this,
            [this](int id, bool checked) {
                if (checked) onModeChanged(id);
            });
    connect(levelSlider_, &QSlider::valueChanged, this,
            [this](int) { refreshLevelLabel(); });

    refreshLevelLabel();
    onModeChanged(1);  // 人机对弈默认
}

void NewGameDialog::onModeChanged(int mode) {
    const bool ai = (mode == 1);
    colorRow_->setEnabled(ai);
    levelRow_->setEnabled(ai);
}

void NewGameDialog::refreshLevelLabel() {
    levelLabel_->setText(QString::fromStdString(vgo::rankLabel(levelSlider_->value())));
}

void NewGameDialog::accept() {
    setup_.mode = (modeGroup_->checkedId() == 0) ? vgo::GameMode::Placement
                                                 : vgo::GameMode::HumanVsAi;
    setup_.size = sizeGroup_->checkedId();
    setup_.level = levelSlider_->value();

    if (setup_.mode == vgo::GameMode::HumanVsAi) {
        const int color = colorGroup_->checkedId();
        if (color == 2) {  // 猜先
            setup_.nigiri = true;
            setup_.human_color = (QRandomGenerator::global()->bounded(2) == 0)
                                     ? vgo::Color::Black
                                     : vgo::Color::White;
        } else {
            setup_.nigiri = false;
            setup_.human_color = (color == 0) ? vgo::Color::Black : vgo::Color::White;
        }
    } else {
        setup_.nigiri = false;
        setup_.human_color = vgo::Color::Black;
    }
    QDialog::accept();
}
