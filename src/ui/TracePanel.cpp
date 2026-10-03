#include "TracePanel.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "ai/agent/trace.h"
#include "model/TraceStore.h"

namespace vgo {

TracePanel::TracePanel(TraceStore* store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setWindowTitle(QStringLiteral("运行 trace（可观测）"));
    resize(780, 480);

    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    auto* clearBtn = new QPushButton(QStringLiteral("清空"), this);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        if (store_) store_->clear();
        refresh();
    });

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(clearBtn);

    auto* v = new QVBoxLayout(this);
    v->addLayout(btnRow);
    v->addWidget(view_, 1);

    refresh();
}

void TracePanel::refresh() {
    if (!store_ || !view_) return;
    const std::vector<AgentTraceRecord> recs = store_->loadRecent(200);

    QStringList lines;
    lines.reserve(static_cast<int>(recs.size()));
    for (const AgentTraceRecord& r : recs) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(r.ts_ms)
                               .toString(QStringLiteral("MM-dd HH:mm:ss.zzz"));
        const QString model =
            r.model.empty() ? QStringLiteral("-") : QString::fromStdString(r.model);
        lines << QStringLiteral("%1  route=%-8s  model=%-16s  pos=%2  latency=%3ms  verify=%4")
                     .arg(ts)
                     .arg(QString::fromStdString(r.route))
                     .arg(model)
                     .arg(QString::fromStdString(r.position_hash).left(8))
                     .arg(r.latency_ms)
                     .arg(QString::fromStdString(r.verify));
    }
    view_->setPlainText(lines.isEmpty()
                            ? QStringLiteral("暂无 trace 记录。接入 LLM 后，每次问答都会在此落一条记录。")
                            : lines.join(QLatin1Char('\n')));
}

}  // namespace vgo
