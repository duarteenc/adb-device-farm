#ifndef FARM_AUTOMATION_MACRORECORDER_H
#define FARM_AUTOMATION_MACRORECORDER_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QVariantMap>

#include "workflowmodel.h"

namespace farm {

/**
 * Turns the operator's interactions on the MASTER device (taps, swipes, long
 * presses, text, keys, navigation) into an editable workflow. Coordinates are
 * recorded as screen fractions so the macro replays on any resolution.
 */
class MacroRecorder : public QObject
{
    Q_OBJECT
public:
    static MacroRecorder &instance();

    void start(const QString &masterId);
    void stop();
    bool isRecording() const { return m_recording; }
    int eventCount() const { return static_cast<int>(m_events.size()); }
    QString masterId() const { return m_master; }

    /// kind: tap | longpress | swipe | text | key | nav  (see FocusPanel::inputRecorded)
    void record(const QString &kind, const QVariantMap &data);
    Workflow toWorkflow(const QString &name) const;
    void clear();

signals:
    void recordingChanged(bool recording);
    void eventRecorded(int count);

private:
    explicit MacroRecorder(QObject *parent = nullptr);
    struct Event
    {
        QString kind;
        QVariantMap data;
        qint64 atMs = 0;
    };
    QList<Event> m_events;
    QElapsedTimer m_clock;
    QString m_master;
    bool m_recording = false;
};

} // namespace farm

#endif // FARM_AUTOMATION_MACRORECORDER_H
