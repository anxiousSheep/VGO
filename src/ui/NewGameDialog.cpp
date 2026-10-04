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
                             int analysis_level, QWidget* parent)
    : QDialog(parent),
      human_vs_ai_(human_vs_ai),
      suggested_level_(std::clamp(suggested_level, 1, 19)),
      suggested_kyu_(suggested_kyu),
      analysis_level_(std::clamp(analysis_level, 2, 19)) {
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

        // 让子（L6）：仅「执黑」时可用（让子石为黑、白先行）。让子数 0/2/3/…/9。
        handicapCombo_ = new QComboBox(this);
        handicapCombo_->addItem(QStringLiteral("0（平手）"), 0);
        for (int h = 2; h <= 9; ++h)
            handicapCombo_->addItem(QStringLiteral("%1 子").arg(h), h);
        handicapRow_ = new QWidget(this);
        auto* handicapLayout = new QHBoxLayout(handicapRow_);
        handicapLayout->setContentsMargins(0, 0, 0, 0);
        handicapLayout->addWidget(handicapCombo_);
        handicapLayout->addStretch(1);
        form->addRow(QStringLiteral("让子"), handicapRow_);

        // 让子仅「执黑」成立；切到执白/猜先时禁用并归 0。
        auto updateHandicap = [this](int colorId) {
            const bool black = (colorId == 0);
            handicapCombo_->setEnabled(black);
            if (!black) handicapCombo_->setCurrentIndex(0);
        };
        connect(colorGroup_, &QButtonGroup::idClicked, this, updateHandicap);
        updateHandicap(0);  // 初始「执黑」选中，让子可用

        // 难度（19 级下拉）+ 自动难度开关（D3.4：据学习者画像自动标定，含新手爬坡）
        autoLevel_ = new QCheckBox(QStringLiteral("自动难度（根据你的棋力推荐）"), this);
        autoLevel_->setChecked(true);

        levelCombo_ = new QComboBox(this);
        for (int lv = 1; lv <= 19; ++lv) {
            if (lv == 1) {
                // D3.4：新手档后动态标注当前棋力（如「新手（20级）」「新手（15级）」），
                // 已过新手时改为趣味提示词。
                levelCombo_->addItem(QString::fromStdString(
                    vgo::noviceLevelLabel(suggested_kyu_, suggested_level_)));
            } else {
                levelCombo_->addItem(QString::fromStdString(vgo::rankLabel(lv)));
            }
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
        const int manual_level = levelCombo_ ? levelCombo_->currentIndex() + 1 : 1;
        // 炸鱼惩罚：强手（业余1段+）手动选新手时，5% 概率匹配职业 AI；自动不触发。
        const double roll = QRandomGenerator::global()->generateDouble();
        setup_.level = vgo::smurfGuardedLevel(manual_level, suggested_level_,
                                              analysis_level_, /*manual=*/true, roll);
        setup_.kyu_hint = 20.0;  // 手动 override：新手 = 固定 20k
    }

    if (human_vs_ai_) {
        const int color = colorGroup_->checkedId();
        // 让子仅「执黑」成立（摆子为黑、白先行）；其余情况归 0。
        setup_.handicap =
            (color == 0 && handicapCombo_) ? handicapCombo_->currentData().toInt() : 0;
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
