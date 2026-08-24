#include "macrorecorder.h"

#include <QUuid>

#include "../core/activitylog.h"

namespace farm {

MacroRecorder &MacroRecorder::instance()
{
    static MacroRecorder recorder;
    return recorder;
}

MacroRecorder::MacroRecorder(QObject *parent)
    : QObject(parent)
{
}

void MacroRecorder::start(const QString &masterId)
{
    m_events.clear();
    m_master = masterId;
    m_recording = true;
    m_clock.start();
    ActivityLog::instance().info(ActivityEntry::Automation, QStringLiteral("Macro recording started on %1").arg(masterId), masterId);
    emit recordingChanged(true);
}

void MacroRecorder::stop()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    ActivityLog::instance().info(ActivityEntry::Automation, QStringLiteral("Macro recording stopped: %1 event(s)").arg(m_events.size()), m_master);
    emit recordingChanged(false);
}

void MacroRecorder::clear()
{
    m_events.clear();
    emit eventRecorded(0);
}

void MacroRecorder::record(const QString &kind, const QVariantMap &data)
{
    if (!m_recording) {
        return;
    }
    Event e;
    e.kind = kind;
    e.data = data;
    e.atMs = m_clock.elapsed();
    // Merge consecutive typed characters into one text event.
    if (kind == QLatin1String("text") && !m_events.isEmpty() && m_events.last().kind == QLatin1String("text") && e.atMs - m_events.last().atMs < 1500) {
        m_events.last().data[QStringLiteral("text")] = m_events.last().data.value(QStringLiteral("text")).toString() + data.value(QStringLiteral("text")).toString();
        m_events.last().atMs = e.atMs;
    } else {
        m_events.append(e);
    }
    emit eventRecorded(static_cast<int>(m_events.size()));
}

Workflow MacroRecorder::toWorkflow(const QString &name) const
{
    Workflow w;
    w.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    w.name = name;
    w.description = QStringLiteral("Recorded on %1").arg(m_master);
    double x = 40;
    const double y = 120;
    QString prev = w.addNode(QStringLiteral("flow.start"), QPointF(x, y));
    qint64 lastMs = 0;
    auto link = [&](const QString &next) {
        w.connectNodes(prev, QStringLiteral("out"), next);
        prev = next;
        x += 230;
    };
    for (const Event &e : m_events) {
        const qint64 gap = e.atMs - lastMs;
        if (gap >= 300) {
            link(w.addNode(QStringLiteral("time.wait"), QPointF(x, y), { { QStringLiteral("ms"), static_cast<int>(gap) } }));
        }
        lastMs = e.atMs;
        QVariantMap p;
        if (e.kind == QLatin1String("tap")) {
            p[QStringLiteral("x")] = e.data.value(QStringLiteral("x"));
            p[QStringLiteral("y")] = e.data.value(QStringLiteral("y"));
            link(w.addNode(QStringLiteral("input.tap"), QPointF(x, y), p));
        } else if (e.kind == QLatin1String("longpress")) {
            p[QStringLiteral("x")] = e.data.value(QStringLiteral("x"));
            p[QStringLiteral("y")] = e.data.value(QStringLiteral("y"));
            p[QStringLiteral("durationMs")] = e.data.value(QStringLiteral("durationMs"));
            link(w.addNode(QStringLiteral("input.longPress"), QPointF(x, y), p));
        } else if (e.kind == QLatin1String("swipe")) {
            p[QStringLiteral("x1")] = e.data.value(QStringLiteral("x"));
            p[QStringLiteral("y1")] = e.data.value(QStringLiteral("y"));
            p[QStringLiteral("x2")] = e.data.value(QStringLiteral("x2"));
            p[QStringLiteral("y2")] = e.data.value(QStringLiteral("y2"));
            p[QStringLiteral("durationMs")] = e.data.value(QStringLiteral("durationMs"));
            link(w.addNode(QStringLiteral("input.swipe"), QPointF(x, y), p));
        } else if (e.kind == QLatin1String("text")) {
            p[QStringLiteral("text")] = e.data.value(QStringLiteral("text"));
            link(w.addNode(QStringLiteral("input.text"), QPointF(x, y), p));
        } else if (e.kind == QLatin1String("key")) {
            const QString key = e.data.value(QStringLiteral("key")).toString();
            static const QHash<QString, QString> map{ { QStringLiteral("Return"), QStringLiteral("KEYCODE_ENTER") }, { QStringLiteral("Enter"), QStringLiteral("KEYCODE_ENTER") },
                                                      { QStringLiteral("Backspace"), QStringLiteral("KEYCODE_DEL") }, { QStringLiteral("Tab"), QStringLiteral("KEYCODE_TAB") },
                                                      { QStringLiteral("Esc"), QStringLiteral("KEYCODE_BACK") }, { QStringLiteral("Space"), QStringLiteral("KEYCODE_SPACE") } };
            p[QStringLiteral("key")] = map.value(key, QStringLiteral("KEYCODE_") + key.toUpper());
            link(w.addNode(QStringLiteral("input.key"), QPointF(x, y), p));
        } else if (e.kind == QLatin1String("nav")) {
            const QString nav = e.data.value(QStringLiteral("name")).toString();
            if (nav == QLatin1String("back")) {
                link(w.addNode(QStringLiteral("input.back"), QPointF(x, y)));
            } else if (nav == QLatin1String("home")) {
                link(w.addNode(QStringLiteral("input.home"), QPointF(x, y)));
            } else if (nav == QLatin1String("recent")) {
                link(w.addNode(QStringLiteral("input.recent"), QPointF(x, y)));
            } else if (nav == QLatin1String("wake")) {
                link(w.addNode(QStringLiteral("device.wake"), QPointF(x, y)));
            } else {
                static const QHash<QString, QString> keys{ { QStringLiteral("volume_up"), QStringLiteral("KEYCODE_VOLUME_UP") }, { QStringLiteral("volume_down"), QStringLiteral("KEYCODE_VOLUME_DOWN") },
                                                           { QStringLiteral("power"), QStringLiteral("KEYCODE_POWER") }, { QStringLiteral("notifications"), QStringLiteral("KEYCODE_NOTIFICATION") },
                                                           { QStringLiteral("screen_off"), QStringLiteral("KEYCODE_SLEEP") } };
                p[QStringLiteral("key")] = keys.value(nav, QStringLiteral("KEYCODE_") + nav.toUpper());
                link(w.addNode(QStringLiteral("input.key"), QPointF(x, y), p));
            }
        }
    }
    link(w.addNode(QStringLiteral("flow.end"), QPointF(x, y)));
    return w;
}

} // namespace farm
