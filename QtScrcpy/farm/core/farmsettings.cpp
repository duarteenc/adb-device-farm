#include "farmsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace farm {

FarmSettings &FarmSettings::instance()
{
    static FarmSettings settings;
    return settings;
}

FarmSettings::FarmSettings(QObject *parent)
    : QObject(parent)
{
    // Portable mode: a `portable` marker next to the executable keeps everything
    // beside the binary; otherwise use the per-user roaming app-data directory.
    const QString portable = QCoreApplication::applicationDirPath() + QStringLiteral("/portable");
    if (QFile::exists(portable)) {
        m_dataDir = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    } else {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (base.isEmpty()) {
            base = QDir::homePath() + QStringLiteral("/.adb-device-farm");
        }
        // Keep a stable folder name independent of the executable/org name.
        m_dataDir = QDir(base).absolutePath();
        if (!m_dataDir.endsWith(QLatin1String("ADBDeviceFarm"))) {
            m_dataDir = QDir(base).absoluteFilePath(QStringLiteral("../ADBDeviceFarm"));
            m_dataDir = QDir::cleanPath(m_dataDir);
        }
    }
    QDir().mkpath(m_dataDir);
}

void FarmSettings::setDataDirectory(const QString &dir)
{
    QMutexLocker lock(&m_mutex);
    m_dataDir = dir;
    QDir().mkpath(m_dataDir);
    delete m_settings;
    m_settings = nullptr;
}

QString FarmSettings::settingsFile() const
{
    return m_dataDir + QStringLiteral("/settings.ini");
}

void FarmSettings::ensureStorage() const
{
    if (!m_settings) {
        m_settings = new QSettings(settingsFile(), QSettings::IniFormat);
    }
}

QVariant FarmSettings::value(const QString &key, const QVariant &fallback) const
{
    QMutexLocker lock(&m_mutex);
    ensureStorage();
    return m_settings->value(key, fallback);
}

void FarmSettings::setValue(const QString &key, const QVariant &value)
{
    {
        QMutexLocker lock(&m_mutex);
        ensureStorage();
        if (m_settings->value(key) == value) {
            return;
        }
        m_settings->setValue(key, value);
        m_settings->sync();
    }
    emit changed(key);
}

QString FarmSettings::automationRunsDirectory() const
{
    return stringValue(QStringLiteral("automation/runsDirectory"), m_dataDir + QStringLiteral("/automation-runs"));
}

QString FarmSettings::screenshotDirectory() const
{
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (fallback.isEmpty()) {
        fallback = m_dataDir;
    }
    return stringValue(QStringLiteral("storage/screenshotDirectory"), fallback + QStringLiteral("/ADBDeviceFarm/screenshots"));
}

QString FarmSettings::recordingDirectory() const
{
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (fallback.isEmpty()) {
        fallback = m_dataDir;
    }
    return stringValue(QStringLiteral("storage/recordingDirectory"), fallback + QStringLiteral("/ADBDeviceFarm/recordings"));
}

} // namespace farm
