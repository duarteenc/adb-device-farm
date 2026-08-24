#ifndef FARM_CORE_FARMLOG_H
#define FARM_CORE_FARMLOG_H

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

namespace farm {

/**
 * Structured, rotating application log.
 *
 *   logs/adb-device-farm.log      (current)
 *   logs/adb-device-farm.1.log    (previous) ... up to maxFiles
 *
 * Each line: `2026-08-24 18:34:03.123 [thread] [level] [component] [device] message`.
 * Installs itself as the Qt message handler (chaining to the previous handler so
 * console/debugger output is preserved) so every qDebug/qWarning ends up on disk too.
 * Thread-safe. Never logs secrets: callers must not pass tokens/passwords.
 */
class FarmLog : public QObject
{
    Q_OBJECT
public:
    enum Level { Debug, Info, Warning, Error };

    static FarmLog &instance();

    /// Open the log directory (created if missing) and install the Qt message hook.
    bool open(const QString &directory, qint64 maxBytes = 5 * 1024 * 1024, int maxFiles = 5);
    void close();

    void write(Level level, const QString &component, const QString &device, const QString &message);
    void debug(const QString &component, const QString &message, const QString &device = QString())
    {
        write(Debug, component, device, message);
    }
    void info(const QString &component, const QString &message, const QString &device = QString())
    {
        write(Info, component, device, message);
    }
    void warning(const QString &component, const QString &message, const QString &device = QString())
    {
        write(Warning, component, device, message);
    }
    void error(const QString &component, const QString &message, const QString &device = QString())
    {
        write(Error, component, device, message);
    }

    QString directory() const { return m_directory; }
    QString currentFile() const;
    void setMinimumLevel(Level level) { m_minLevel = level; }

    /// Detects an unclean shutdown from the previous session (lock file left behind).
    bool previousSessionCrashed() const { return m_previousCrash; }
    /// Called at clean exit so the next start doesn't report a crash.
    void markCleanShutdown();

private:
    explicit FarmLog(QObject *parent = nullptr);
    ~FarmLog() override;
    void rotateIfNeeded();
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

    QMutex m_mutex;
    QFile m_file;
    QString m_directory;
    qint64 m_maxBytes = 5 * 1024 * 1024;
    int m_maxFiles = 5;
    Level m_minLevel = Debug;
    bool m_previousCrash = false;
    QtMessageHandler m_previousHandler = nullptr;
};

} // namespace farm

#endif // FARM_CORE_FARMLOG_H
