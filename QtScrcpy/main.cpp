#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTranslator>
#include <QDateTime>

#include "config.h"
#include "dialog.h"
#include "farmwindow.h"
#include "mousetap/mousetap.h"

static Dialog *g_mainDlg = Q_NULLPTR;
static QtMessageHandler g_oldMessageHandler = Q_NULLPTR;
void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg);
void installTranslator();

static QtMsgType g_msgType = QtInfoMsg;
QtMsgType covertLogLevel(const QString &logLevel);

int main(int argc, char *argv[])
{
    // set env
#ifdef Q_OS_WIN32
    qputenv("QTSCRCPY_ADB_PATH", "../../../QtScrcpy/QtScrcpyCore/src/third_party/adb/win/adb.exe");
    qputenv("QTSCRCPY_SERVER_PATH", "../../../QtScrcpy/QtScrcpyCore/src/third_party/scrcpy-server");
    qputenv("QTSCRCPY_KEYMAP_PATH", "../../../keymap");
    qputenv("QTSCRCPY_CONFIG_PATH", "../../../config");
#endif

#ifdef Q_OS_OSX
    qputenv("QTSCRCPY_ADB_PATH", "../../../../../../QtScrcpy/QtScrcpyCore/src/third_party/adb/mac/adb");
    qputenv("QTSCRCPY_SERVER_PATH", "../../../../../../QtScrcpy/QtScrcpyCore/src/third_party/scrcpy-server");
    qputenv("QTSCRCPY_KEYMAP_PATH", "../../../../../../keymap");
    qputenv("QTSCRCPY_CONFIG_PATH", "../../../../../../config");
#endif

#ifdef Q_OS_LINUX
    // Only set environment variables if they are not already set (e.g., by AppImage AppRun)
    if (qgetenv("QTSCRCPY_ADB_PATH").isEmpty()) {
        qputenv("QTSCRCPY_ADB_PATH", "../../../QtScrcpy/QtScrcpyCore/src/third_party/adb/linux/adb");
    }
    if (qgetenv("QTSCRCPY_SERVER_PATH").isEmpty()) {
        qputenv("QTSCRCPY_SERVER_PATH", "../../../QtScrcpy/QtScrcpyCore/src/third_party/scrcpy-server");
    }
    if (qgetenv("QTSCRCPY_KEYMAP_PATH").isEmpty()) {
        qputenv("QTSCRCPY_KEYMAP_PATH", "../../../keymap");
    }
    if (qgetenv("QTSCRCPY_CONFIG_PATH").isEmpty()) {
        qputenv("QTSCRCPY_CONFIG_PATH", "../../../config");
    }
#endif

    g_msgType = covertLogLevel(Config::getInstance().getLogLevel());

    // set on QApplication before
    // bug: config path is error on mac
    int opengl = Config::getInstance().getDesktopOpenGL();
    if (0 == opengl) {
        QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    } else if (1 == opengl) {
        QApplication::setAttribute(Qt::AA_UseOpenGLES);
    } else if (2 == opengl) {
        QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    }

    // The farm shows one QOpenGLWidget per device. Share a single GL context
    // across them instead of letting each create its own — without this,
    // mirroring dozens of devices at once exhausts GPU contexts and crashes.
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

#if (QT_VERSION >= QT_VERSION_CHECK(5,14,0))
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
#endif

    QSurfaceFormat varFormat = QSurfaceFormat::defaultFormat();
    varFormat.setVersion(2, 0);
    varFormat.setProfile(QSurfaceFormat::NoProfile);
    /*
    varFormat.setSamples(4);
    varFormat.setAlphaBufferSize(8);
    varFormat.setBlueBufferSize(8);
    varFormat.setRedBufferSize(8);
    varFormat.setGreenBufferSize(8);
    varFormat.setDepthBufferSize(24);
    */
    QSurfaceFormat::setDefaultFormat(varFormat);

    g_oldMessageHandler = qInstallMessageHandler(myMessageOutput);
    QApplication a(argc, argv);

    // Application icon (title bar / taskbar) for all platforms.
    a.setWindowIcon(QIcon(":/QtScrcpy.ico"));

    // windows下通过qmake VERSION变量或者rc设置版本号和应用名称后，这里可以直接拿到
    // mac下拿到的是CFBundleVersion的值
    qDebug() << a.applicationVersion();
    qDebug() << a.applicationName();

    //update version
    QStringList versionList = QCoreApplication::applicationVersion().split(".");
    if (versionList.size() >= 3) {
        QString version = versionList[0] + "." + versionList[1] + "." + versionList[2];
        a.setApplicationVersion(version);
    }

    installTranslator();
#if defined(Q_OS_WIN32) || defined(Q_OS_OSX)
    MouseTap::getInstance()->initMouseEventTap();
#endif

    // load style sheet
    QFile file(":/qss/psblack.css");
    if (file.open(QFile::ReadOnly)) {
        QString qss = QLatin1String(file.readAll());
        QString paletteColor = qss.mid(20, 7);
        qApp->setPalette(QPalette(QColor(paletteColor)));
        qApp->setStyleSheet(qss);
        file.close();
    }

    qsc::AdbProcess::setAdbPath(Config::getInstance().getAdbPath());

    // v2.0 device-farm dashboard: opt-in via the --farm flag, or implicitly when
    // the executable is the branded 333Farmer build (e.g. 333Farmer.exe).
    const bool farmByName =
        QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName().contains(
            "Farmer", Qt::CaseInsensitive);
    if (a.arguments().contains("--farm") || farmByName) {
        // Startup splash: branded loading screen while the dashboard builds and the
        // first `adb devices` runs (which then auto-connects every phone).
        const int kSplashW = 440;
        const int kSplashH = 200;
        QPixmap splashPix(kSplashW, kSplashH);
        splashPix.fill(QColor(0x0f, 0x14, 0x22));    // matches the app background
        {
            QPainter p(&splashPix);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QColor(0xff, 0xff, 0xff));
            QFont titleFont = p.font();
            titleFont.setPointSize(30);
            titleFont.setBold(true);
            p.setFont(titleFont);
            // "333Farmer" centred, leaving room for the status message at the bottom.
            p.drawText(QRect(0, 0, kSplashW, kSplashH - 30), Qt::AlignCenter,
                       QStringLiteral("333Farmer"));
        }

        QSplashScreen splash(splashPix);
        splash.show();
        splash.showMessage(QObject::tr("Conectando teléfonos…"),
                           Qt::AlignBottom | Qt::AlignHCenter, QColor(0x9c, 0xb3, 0xd6));
        a.processEvents();

        auto *farm = new FarmWindow();
        farm->resize(1280, 820);
        farm->showMaximized();

        // Close the splash once the first device list arrives (phones start
        // connecting), with a hard fallback so it never lingers.
        QObject::connect(farm, &FarmWindow::firstDevicesReady, &splash,
                         [&splash, farm]() { splash.finish(farm); });
        QTimer::singleShot(6000, &splash, [&splash, farm]() { splash.finish(farm); });

        int farmRet = a.exec();
        delete farm;
#if defined(Q_OS_WIN32) || defined(Q_OS_OSX)
        MouseTap::getInstance()->quitMouseEventTap();
#endif
        return farmRet;
    }

    g_mainDlg = new Dialog {};
    g_mainDlg->show();

    qInfo() << QObject::tr("This software is completely open source and free. Use it at your own risk. You can download it at the "
            "following address:");
    qInfo() << QString("QtScrcpy %1 <https://github.com/barry-ran/QtScrcpy>").arg(QCoreApplication::applicationVersion());

    qInfo() << QObject::tr("If you need more professional batch control mirror software, you can try the following software:");
    qInfo() << QString(QObject::tr("QuickMirror") + " <https://lrbnfell4p.feishu.cn/drive/folder/KviYfz5uFlpUT8dXgdjccmfUnse>");

    qInfo() << QObject::tr("If you need more professional game keymap mirror software, you can try the following software:");
    qInfo() << QString(QObject::tr("QuickAssistant") + " <https://lrbnfell4p.feishu.cn/drive/folder/Hqckfxj5el1Wjpd9uezcX71lnBh>");

    qInfo() << QObject::tr("If you need more professional PC remote software, you can try the following software:");
    qInfo() << QString(QObject::tr("QuickDesk") + " <https://github.com/barry-ran/QuickDesk>");

    qInfo() << QObject::tr("You can contact me with telegram <https://t.me/+Ylf_5V_rDCMyODQ1>");

    int ret = a.exec();
    delete g_mainDlg;

#if defined(Q_OS_WIN32) || defined(Q_OS_OSX)
    MouseTap::getInstance()->quitMouseEventTap();
#endif
    return ret;
}

void installTranslator()
{
    static QTranslator translator;
    QLocale locale;
    QLocale::Language language = locale.language();

    if (Config::getInstance().getLanguage() == "zh_CN") {
        language = QLocale::Chinese;
    } else if (Config::getInstance().getLanguage() == "en_US") {
        language = QLocale::English;
    } else if (Config::getInstance().getLanguage() == "ja_JP") {
        language = QLocale::Japanese;
    }

    QString languagePath = ":/i18n/";
    switch (language) {
    case QLocale::Chinese:
        languagePath += "zh_CN.qm";
        break;
    case QLocale::Japanese:
        languagePath += "ja_JP.qm";
        break;
    case QLocale::English:
    default:
        languagePath += "en_US.qm";
        break;
    }

    auto loaded = translator.load(languagePath);
    if (!loaded) {
        qWarning() << "Failed to load translation file:" << languagePath;
    }
    qApp->installTranslator(&translator);
}

QtMsgType covertLogLevel(const QString &logLevel)
{
    if ("debug" == logLevel) {
        return QtDebugMsg;
    }

    if ("info" == logLevel) {
        return QtInfoMsg;
    }

    if ("warn" == logLevel) {
        return QtWarningMsg;
    }

    if ("error" == logLevel) {
        return QtCriticalMsg;
    }

#ifdef QT_NO_DEBUG
    return QtInfoMsg;
#else
    return QtDebugMsg;
#endif
}

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString outputMsg;
    
#ifdef ENABLE_DETAILED_LOGS
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    
    if (context.file && context.line > 0) {
        QString fileName = QString::fromUtf8(context.file);

        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash >= 0) {
            fileName = fileName.mid(lastSlash + 1);
        }
        lastSlash = fileName.lastIndexOf('\\');
        if (lastSlash >= 0) {
            fileName = fileName.mid(lastSlash + 1);
        }
        
        outputMsg = QString("[ %1 %2: %3 ] %4").arg(timestamp).arg(fileName).arg(context.line).arg(msg);
    } else {
        outputMsg = QString("[%1] %2").arg(timestamp).arg(msg);
    }

    switch (type) {
    case QtDebugMsg:
        outputMsg.prepend("[debug] ");
        break;
    case QtInfoMsg:
        outputMsg.prepend("[info] ");
        break;
    case QtWarningMsg:
        outputMsg.prepend("[warring] ");
        break;
    case QtCriticalMsg:
        outputMsg.prepend("[critical] ");
        break;
    case QtFatalMsg:
        outputMsg.prepend("[fatal] ");
        break;
    }

    fprintf(stderr, "%s\n", outputMsg.toUtf8().constData());
#else
    outputMsg = msg;
    if (g_oldMessageHandler) {
        g_oldMessageHandler(type, context, outputMsg);
    }
#endif

    // Is Qt log level higher than warning?
    float fLogLevel = g_msgType;
    if (QtInfoMsg == g_msgType) {
        fLogLevel = QtDebugMsg + 0.5f;
    }
    float fLogLevel2 = type;
    if (QtInfoMsg == type) {
        fLogLevel2 = QtDebugMsg + 0.5f;
    }

    if (fLogLevel <= fLogLevel2) {
        if (g_mainDlg && g_mainDlg->isVisible() && !g_mainDlg->filterLog(outputMsg)) {
            g_mainDlg->outLog(outputMsg);
        }
    }

    if (QtFatalMsg == type) {
        //abort();
    }
}
