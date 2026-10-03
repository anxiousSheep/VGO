#include "NewGameDialog.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

#include "ai/strength.h"

NewGameDialog::NewGameDialog(bool human_vs_ai, int suggested_level, double suggested_kyu,
                             QWidget* parent)
    : QDialog(parent),
      human_vs_ai_(human_vs_ai),
      suggested_level_(std::clamp(suggested_level, 1, 19)),
      suggested_kyu_(suggested_kyu) {
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

        // 难度（19 级下拉）+ 自动难度开关（D3.4：据学习者画像自动标定，含新手爬坡）
        autoLevel_ = new QCheckBox(QStringLiteral("自动难度（根据你的棋力推荐）"), this);
        autoLevel_->setChecked(true);

        levelCombo_ = new QComboBox(this);
        for (int lv = 1; lv <= 19; ++lv) {
            levelCombo_->addItem(QString::fromStdString(vgo::rankLabel(lv)));
        }
        levelCombo_->setCurrentIndex(suggested_level_ - 1);
        levelCombo_->setEnabled(false);  // 自动难度默认启用，下拉禁用

        connect(autoLevel_, &QCheckBox::toggled, this,
                [this](bool on) { levelCombo_->setEnabled(!on); });

        levelRow_ = new QWidget(this);
        auto* levelLayout = new QHBoxLayout(levelRow_);
        levelLayout->setContentsMargins(0, 0, 0, 0);
        levelLayout->addWidget(levelCombo_, 1);
        form->addRow(autoLevel_);
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
    // D3.4：自动难度用画像估计（含新手爬坡 kyu），手动则固定新手 = 20k。
    if (autoLevel_ && autoLevel_->isChecked()) {
        setup_.level = suggested_level_;
        setup_.kyu_hint = suggested_kyu_;
    } else {
        setup_.level = levelCombo_ ? levelCombo_->currentIndex() + 1 : 1;
        setup_.kyu_hint = 20.0;  // 手动 override：新手 = 固定 20k
    }

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
