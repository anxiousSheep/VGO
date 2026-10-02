#include "NewGameDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

#include "ai/strength.h"

NewGameDialog::NewGameDialog(bool human_vs_ai, QWidget* parent)
    : QDialog(parent), human_vs_ai_(human_vs_ai) {
    setWindowTitle(QStringLiteral("新对局"));

    // 棋盘大小
    auto* size9 = new QRadioButton(QStringLiteral("9×9"), this);
    auto* size13 = new QRadioButton(QStringLiteral("13×13"), this);
    auto* size19 = new QRadioButton(QStringLiteral("19×19"), this);
    size9->setChecked(true);
    sizeGroup_ = new QButtonGroup(this);
    sizeGroup_->addButton(size9, 9);
    sizeGroup_->addButton(size13, 13);
    sizeGroup_->addButton(size19, 19);

    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(size9);
    sizeRow->addWidget(size13);
    sizeRow->addWidget(size19);
    sizeRow->addStretch(1);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("棋盘大小"), sizeRow);

    if (human_vs_ai_) {
        // 执子（仅人机对弈）
        auto* blackRadio = new QRadioButton(QStringLiteral("执黑"), this);
        auto* whiteRadio = new QRadioButton(QStringLiteral("执白"), this);
        auto* nigiriRadio = new QRadioButton(QStringLiteral("猜先"), this);
        blackRadio->setChecked(true);
        colorGroup_ = new QButtonGroup(this);
        colorGroup_->addButton(blackRadio, 0);
        colorGroup_->addButton(whiteRadio, 1);
        colorGroup_->addButton(nigiriRadio, 2);

        colorRow_ = new QWidget(this);
        auto* colorLayout = new QHBoxLayout(colorRow_);
        colorLayout->setContentsMargins(0, 0, 0, 0);
        colorLayout->addWidget(blackRadio);
        colorLayout->addWidget(whiteRadio);
        colorLayout->addWidget(nigiriRadio);
        colorLayout->addStretch(1);
        form->addRow(QStringLiteral("执子"), colorRow_);

        // 难度（19 级下拉）
        levelCombo_ = new QComboBox(this);
        for (int lv = 1; lv <= 19; ++lv) {
            levelCombo_->addItem(QString::fromStdString(vgo::rankLabel(lv)));
        }
        levelCombo_->setCurrentIndex(0);

        levelRow_ = new QWidget(this);
        auto* levelLayout = new QHBoxLayout(levelRow_);
        levelLayout->setContentsMargins(0, 0, 0, 0);
        levelLayout->addWidget(levelCombo_, 1);
        form->addRow(QStringLiteral("难度"), levelRow_);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &NewGameDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewGameDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
}

void NewGameDialog::accept() {
    setup_.mode = human_vs_ai_ ? vgo::GameMode::HumanVsAi : vgo::GameMode::Placement;
    setup_.size = sizeGroup_->checkedId();
    setup_.level = levelCombo_ ? levelCombo_->currentIndex() + 1 : 1;

    if (human_vs_ai_) {
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
