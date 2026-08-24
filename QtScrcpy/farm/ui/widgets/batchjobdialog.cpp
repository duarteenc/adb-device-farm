#include "batchjobdialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "devices/deviceregistry.h"
#include "ui/farmtheme.h"

namespace farm {

BatchJobDialog::BatchJobDialog(BatchJob *job, QWidget *parent)
    : QDialog(parent)
    , m_job(job)
{
    setWindowTitle(job->name());
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 460);
    theme::apply(this);

    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("font-weight:bold; font-size:13px;"));
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({ tr("#"), tr("Device"), tr("Status"), tr("Time"), tr("Result") });
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);

    auto *buttons = new QHBoxLayout();
    m_pauseBtn = theme::button(tr("Pause"), this);
    m_cancelBtn = theme::button(tr("Cancel job"), this, QStringLiteral("danger"));
    m_retryBtn = theme::button(tr("Retry failed"), this, QStringLiteral("primary"));
    auto *closeBtn = theme::button(tr("Close"), this);
    buttons->addWidget(m_pauseBtn);
    buttons->addWidget(m_cancelBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_retryBtn);
    buttons->addWidget(closeBtn);

    layout->addWidget(m_summary);
    layout->addWidget(m_bar);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);

    connect(m_pauseBtn, &QPushButton::clicked, this, [this]() {
        if (!m_job) {
            return;
        }
        if (m_job->status() == BatchJob::Running) {
            m_job->pause();
        } else if (m_job->status() == BatchJob::Paused) {
            m_job->resume();
        }
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_job) {
            m_job->cancel();
        }
    });
    connect(m_retryBtn, &QPushButton::clicked, this, [this]() {
        if (m_job) {
            m_job->retryFailed();
        }
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(job, &BatchJob::progressChanged, this, &BatchJobDialog::refresh);
    connect(job, &BatchJob::statusChanged, this, [this](BatchJob::Status) { refresh(); });
    connect(job, &BatchJob::itemChanged, this, &BatchJobDialog::refreshItem);
    connect(job, &QObject::destroyed, this, [this]() { close(); });

    rebuildRows();
    refresh();
}

BatchJobDialog *BatchJobDialog::show(BatchJob *job, QWidget *parent)
{
    auto *dlg = new BatchJobDialog(job, parent);
    dlg->QDialog::show();
    return dlg;
}

void BatchJobDialog::rebuildRows()
{
    if (!m_job) {
        return;
    }
    const QList<BatchJob::Item> items = m_job->items();
    m_table->setRowCount(static_cast<int>(items.size()));
    m_rows.clear();
    for (int i = 0; i < items.size(); ++i) {
        const BatchJob::Item &it = items.at(i);
        const DeviceRecord r = DeviceRegistry::instance().get(it.id);
        m_table->setItem(i, 0, new QTableWidgetItem(r.numberString()));
        m_table->setItem(i, 1, new QTableWidgetItem(r.displayName() + QStringLiteral("  ") + it.id));
        m_table->setItem(i, 2, new QTableWidgetItem());
        m_table->setItem(i, 3, new QTableWidgetItem());
        m_table->setItem(i, 4, new QTableWidgetItem());
        m_rows.insert(it.id, i);
        refreshItem(it.id);
    }
}

void BatchJobDialog::refreshItem(const QString &id)
{
    if (!m_job || !m_rows.contains(id)) {
        return;
    }
    const int row = m_rows.value(id);
    const BatchJob::Item it = m_job->item(id);
    QTableWidgetItem *status = m_table->item(row, 2);
    status->setText(BatchJob::itemStatusName(it.status));
    QColor c = theme::textMuted();
    if (it.status == BatchJob::Succeeded) {
        c = theme::success();
    } else if (it.status == BatchJob::Failed_) {
        c = theme::danger();
    } else if (it.status == BatchJob::InProgress) {
        c = theme::warning();
    }
    status->setForeground(c);
    m_table->item(row, 3)->setText(it.durationMs > 0 ? QStringLiteral("%1 ms").arg(it.durationMs) : QString());
    m_table->item(row, 4)->setText(it.message.simplified().left(300));
    m_table->item(row, 4)->setToolTip(it.message);
}

void BatchJobDialog::refresh()
{
    if (!m_job) {
        return;
    }
    m_summary->setText(QStringLiteral("%1 — %2 [%3]").arg(m_job->name(), m_job->summary(), BatchJob::statusName(m_job->status())));
    m_bar->setValue(m_job->percent());
    const BatchJob::Status s = m_job->status();
    m_pauseBtn->setText(s == BatchJob::Paused ? tr("Resume") : tr("Pause"));
    m_pauseBtn->setEnabled(s == BatchJob::Running || s == BatchJob::Paused);
    m_cancelBtn->setEnabled(s == BatchJob::Running || s == BatchJob::Paused || s == BatchJob::Pending);
    m_retryBtn->setEnabled((s == BatchJob::Completed || s == BatchJob::Failed || s == BatchJob::Cancelled) && m_job->failed() + m_job->cancelled() > 0);
}

} // namespace farm
