#include "ReviewDialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "ai/learner/learner_model.h"
#include "model/LearnerStore.h"

using namespace vgo;

ReviewDialog::ReviewDialog(LearnerStore* learner, int today, QWidget* parent)
    : QDialog(parent), learner_(learner), today_(today) {
    setWindowTitle(QStringLiteral("今日一题 · 复习"));
    resize(560, 480);

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);

    auto* host = new QWidget(this);
    listLayout_ = new QVBoxLayout(host);
    listLayout_->setContentsMargins(0, 0, 0, 0);
    listLayout_->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(host);

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* v = new QVBoxLayout(this);
    v->addWidget(summary_);
    v->addWidget(scroll, 1);
    v->addWidget(closeBtn);

    rebuild();
}

void ReviewDialog::rebuild() {
    if (!learner_ || !listLayout_) return;

    // 先清空旧列表（保留末尾的 stretch）。
    while (listLayout_->count() > 1) {
        QLayoutItem* item = listLayout_->takeAt(0);
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    const LearnerModel& model = learner_->model();
    const std::vector<const ReviewItem*> due = model.dueItems(today_);
    const auto weak = model.topWeakAreas(3);

    QString summary = QStringLiteral("今日到期 %1 题。").arg(static_cast<int>(due.size()));
    if (!weak.empty()) {
        summary += QStringLiteral(" 近期弱项：");
        QStringList areas;
        for (const auto& w : weak)
            areas << QString::fromUtf8(weakAreaLabel(w.first));
        summary += areas.join(QStringLiteral("、"));
    }
    summary_->setText(summary);

    for (const ReviewItem* it : due) {
        auto* card = new QFrame;
        card->setFrameShape(QFrame::StyledPanel);

        auto* label = new QLabel(
            QStringLiteral("【%1】%2")
                .arg(QString::fromUtf8(weakAreaLabel(it->area)),
                     QString::fromStdString(it->prompt)),
            card);
        label->setWordWrap(true);

        auto* again = new QPushButton(QStringLiteral("又忘了"), card);
        auto* ok = new QPushButton(QStringLiteral("勉强记得"), card);
        auto* easy = new QPushButton(QStringLiteral("轻松记住"), card);

        const std::string id = it->id;
        auto grade = [this, id](int quality) {
            if (!learner_) return;
            learner_->model().review(id, quality, today_);
            learner_->save();
            rebuild();
        };
        connect(again, &QPushButton::clicked, this, [grade] { grade(0); });
        connect(ok, &QPushButton::clicked, this, [grade] { grade(3); });
        connect(easy, &QPushButton::clicked, this, [grade] { grade(5); });

        auto* row = new QHBoxLayout;
        row->addWidget(again);
        row->addWidget(ok);
        row->addWidget(easy);

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->addWidget(label);
        cardLayout->addLayout(row);

        listLayout_->insertWidget(listLayout_->count() - 1, card);
    }
}
