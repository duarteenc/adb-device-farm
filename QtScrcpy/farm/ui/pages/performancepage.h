#ifndef FARM_UI_PERFORMANCEPAGE_H
#define FARM_UI_PERFORMANCEPAGE_H

#include <QHash>
#include <QList>
#include <QWidget>

#include "performance/perfmonitor.h"

class QLabel;
class QTableWidget;

namespace farm {

class SparkLine;

/**
 * Performance Center: live metrics (CPU, RAM, decode/render/drop fps, latency,
 * ADB throughput, connect latency, reconnects, scan duration, queues, UI lag),
 * sparklines over the last minutes, per-device fps/latency table, JSON export
 * and the diagnostics bundle.
 */
class PerformancePage : public QWidget
{
    Q_OBJECT
public:
    explicit PerformancePage(QWidget *parent = nullptr);

private:
    QWidget *metric(const QString &key, const QString &label, bool spark);
    void onSample(const PerfSnapshot &s);
    void refreshDevices();
    void exportDiagnostics();

    QHash<QString, QLabel *> m_values;
    QHash<QString, SparkLine *> m_sparks;
    QTableWidget *m_devices = nullptr;
    QLabel *m_lanes = nullptr;
};

/// Tiny QPainter line chart for a metric history.
class SparkLine : public QWidget
{
    Q_OBJECT
public:
    explicit SparkLine(QWidget *parent = nullptr);
    void push(double value);
    void setColor(const QColor &c) { m_color = c; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<double> m_values;
    QColor m_color;
};

} // namespace farm

#endif // FARM_UI_PERFORMANCEPAGE_H
