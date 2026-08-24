#ifndef FARM_UI_BATCHJOBDIALOG_H
#define FARM_UI_BATCHJOBDIALOG_H

#include <QDialog>
#include <QPointer>

#include "core/batchjob.h"

class QLabel;
class QProgressBar;
class QTableWidget;
class QPushButton;

namespace farm {

/**
 * Live view of one BatchJob: "62 / 80 complete · 59 ok · 3 failed", per-device
 * rows with status + message, Pause/Resume, Cancel and "Retry failed".
 * Non-modal so the operator keeps working; closing never cancels the job.
 */
class BatchJobDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchJobDialog(BatchJob *job, QWidget *parent = nullptr);
    static BatchJobDialog *show(BatchJob *job, QWidget *parent);

private:
    void refresh();
    void refreshItem(const QString &id);
    void rebuildRows();

    QPointer<BatchJob> m_job;
    QLabel *m_summary = nullptr;
    QProgressBar *m_bar = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QHash<QString, int> m_rows;
};

} // namespace farm

#endif // FARM_UI_BATCHJOBDIALOG_H
