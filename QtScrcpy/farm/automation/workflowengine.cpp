#include "workflowengine.h"

#include <algorithm>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QThread>
#include <QUuid>

#include "../adb/adbexecutor.h"
#include "../adb/adbparsers.h"
#include "../adb/adbquote.h"
#include "../core/activitylog.h"
#include "../core/farmlog.h"
#include "../core/farmsettings.h"
#include "../devices/devicecommands.h"
#include "../devices/deviceregistry.h"
#include "../devices/deviceservice.h"
#include "../devices/keepawakemanager.h"
#include "expression.h"
#include "imagematcher.h"
#include "nodecatalog.h"
#include "ocrprovider.h"
#include "uihierarchy.h"

namespace farm {

// =====================================================================
//  Per-device execution context (worker thread)
// =====================================================================
namespace {

struct NodeResult
{
    bool ok = true;
    QString port = QStringLiteral("out");
    QString message;
    QString error;
};

struct LoopFrame
{
    QString nodeId;
    int iteration = 0;
    int limit = 0;
};

class DeviceRunContext
{
public:
    AutomationRun *run;
    QString deviceId;
    QVariantMap vars;
    QString deviceDir;
    int step = 0;
    int depth = 0;
    QSize screen;    // physical size of the screenshots (pixels)
    QList<LoopFrame> loops;
    QString pendingBreak;    // "break" / "continue" propagated from inside a loop body

    bool cancelled() const { return run->isCancelled(); }

    void waitWhilePaused()
    {
        while (run->isPaused() && !cancelled()) {
            QThread::msleep(100);
        }
    }

    AdbResult shell(const QString &script, int timeoutMs)
    {
        AdbCommand c;
        c.serial = deviceId;
        c.args << QStringLiteral("shell") << script;
        c.timeoutMs = timeoutMs > 0 ? timeoutMs : 30000;
        c.label = QStringLiteral("wf-shell");
        return AdbExecutor::instance().runSync(c, run->token());
    }

    AdbResult adb(const QStringList &args, int timeoutMs, bool binary = false)
    {
        AdbCommand c;
        c.serial = deviceId;
        c.args = args;
        c.timeoutMs = timeoutMs > 0 ? timeoutMs : 60000;
        c.binaryOutput = binary;
        c.label = QStringLiteral("wf-adb");
        return AdbExecutor::instance().runSync(c, run->token());
    }

    QImage screenshot(QString *error = nullptr)
    {
        const AdbResult r = adb({ QStringLiteral("exec-out"), QStringLiteral("screencap"), QStringLiteral("-p") }, 20000, true);
        if (!r.ok) {
            if (error) {
                *error = r.error;
            }
            return QImage();
        }
        QImage img = QImage::fromData(r.rawStdOut, "PNG");
        if (img.isNull() && error) {
            *error = QStringLiteral("screencap returned no image");
        }
        if (!img.isNull()) {
            screen = img.size();
        }
        return img;
    }

    QString saveImage(const QImage &img, const QString &tag)
    {
        QDir().mkpath(deviceDir);
        const QString path = QStringLiteral("%1/%2-step%3.png").arg(deviceDir, tag).arg(step);
        img.save(path, "PNG");
        return path;
    }

    // Fractions (0..1) become pixels of the real screen; values > 1 are pixels already.
    QPoint toPixels(double x, double y)
    {
        if ((x <= 1.0 && y <= 1.0) || screen.isEmpty()) {
            if (screen.isEmpty()) {
                const DeviceRecord rec = DeviceRegistry::instance().get(deviceId);
                if (!rec.screenSize.isEmpty()) {
                    screen = rec.screenSize;
                } else {
                    const AdbResult r = shell(QStringLiteral("wm size"), 8000);
                    const QString wm = adb::parseWmSize(r.stdOut);
                    const QStringList parts = wm.split(QLatin1Char('x'));
                    screen = parts.size() == 2 ? QSize(parts.at(0).toInt(), parts.at(1).toInt()) : QSize(1080, 1920);
                }
            }
            if (x <= 1.0 && y <= 1.0) {
                return QPoint(static_cast<int>(x * screen.width()), static_cast<int>(y * screen.height()));
            }
        }
        return QPoint(static_cast<int>(x), static_cast<int>(y));
    }

    QString str(const WorkflowNode &n, const QString &key)
    {
        return Expression::substitute(n.params.value(key).toString(), vars);
    }
    double num(const WorkflowNode &n, const QString &key, double def = 0)
    {
        const QVariant v = Expression::value(n.params.value(key).toString(), vars);
        bool ok = false;
        const double d = v.toDouble(&ok);
        return ok ? d : def;
    }
    int integer(const WorkflowNode &n, const QString &key, int def = 0)
    {
        return static_cast<int>(num(n, key, def));
    }
    bool flag(const WorkflowNode &n, const QString &key, bool def = false)
    {
        const QVariant v = n.params.value(key);
        return v.isValid() ? Expression::toBool(v) : def;
    }

    void log(const QString &nodeTitle, const QString &status, const QString &message, const QString &error, qint64 durationMs, const QString &screenshot = QString())
    {
        JobLogRow row;
        row.runId = run->id();
        row.device = deviceId;
        row.time = QDateTime::currentDateTime();
        row.step = QStringLiteral("%1 %2").arg(step).arg(nodeTitle);
        row.status = status;
        row.durationMs = durationMs;
        row.message = message;
        row.error = error;
        row.screenshot = screenshot;
        run->reportLog(row);
    }

    QRect regionFromParam(const QString &spec, const QSize &size)
    {
        const QStringList parts = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() != 4) {
            return QRect();
        }
        double v[4];
        for (int i = 0; i < 4; ++i) {
            v[i] = parts.at(i).trimmed().toDouble();
        }
        if (v[0] <= 1.0 && v[1] <= 1.0 && v[2] <= 1.0 && v[3] <= 1.0) {
            return QRect(static_cast<int>(v[0] * size.width()), static_cast<int>(v[1] * size.height()), static_cast<int>(v[2] * size.width()), static_cast<int>(v[3] * size.height()));
        }
        return QRect(static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), static_cast<int>(v[3]));
    }

    QString assetPath(const QString &p)
    {
        if (QFileInfo(p).isAbsolute() || QFileInfo::exists(p)) {
            return p;
        }
        const QString inRuns = FarmSettings::instance().dataDirectory() + QStringLiteral("/assets/") + p;
        return QFileInfo::exists(inRuns) ? inRuns : p;
    }

    QVariantMap matchToVar(const ImageMatch &m)
    {
        QVariantMap v;
        v[QStringLiteral("x")] = screen.isEmpty() ? m.center().x() : m.center().x() / screen.width();
        v[QStringLiteral("y")] = screen.isEmpty() ? m.center().y() : m.center().y() / screen.height();
        v[QStringLiteral("px")] = static_cast<int>(m.center().x());
        v[QStringLiteral("py")] = static_cast<int>(m.center().y());
        v[QStringLiteral("width")] = m.rect.width();
        v[QStringLiteral("height")] = m.rect.height();
        v[QStringLiteral("score")] = m.score;
        return v;
    }

    QVariantMap rectToVar(const QRect &r)
    {
        QVariantMap v;
        v[QStringLiteral("x")] = screen.isEmpty() ? r.center().x() : static_cast<double>(r.center().x()) / screen.width();
        v[QStringLiteral("y")] = screen.isEmpty() ? r.center().y() : static_cast<double>(r.center().y()) / screen.height();
        v[QStringLiteral("px")] = r.center().x();
        v[QStringLiteral("py")] = r.center().y();
        v[QStringLiteral("width")] = r.width();
        v[QStringLiteral("height")] = r.height();
        return v;
    }
};

// ---------------------------------------------------------------- node executors

NodeResult fail(const QString &error)
{
    NodeResult r;
    r.ok = false;
    r.error = error;
    return r;
}

NodeResult okPort(const QString &port, const QString &message = QString())
{
    NodeResult r;
    r.port = port;
    r.message = message;
    return r;
}

NodeResult tapAt(DeviceRunContext &ctx, const QPoint &p)
{
    const AdbResult r = ctx.shell(QStringLiteral("input tap %1 %2").arg(p.x()).arg(p.y()), 10000);
    return r.ok ? okPort(QStringLiteral("out"), QStringLiteral("tap %1,%2").arg(p.x()).arg(p.y())) : fail(r.error);
}

NodeResult execInput(DeviceRunContext &ctx, const WorkflowNode &n)
{
    const QString t = n.type;
    if (t == QLatin1String("input.tap")) {
        return tapAt(ctx, ctx.toPixels(ctx.num(n, QStringLiteral("x")), ctx.num(n, QStringLiteral("y"))));
    }
    if (t == QLatin1String("input.doubleTap")) {
        const QPoint p = ctx.toPixels(ctx.num(n, QStringLiteral("x")), ctx.num(n, QStringLiteral("y")));
        const AdbResult r = ctx.shell(QStringLiteral("input tap %1 %2; input tap %1 %2").arg(p.x()).arg(p.y()), 10000);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    if (t == QLatin1String("input.longPress")) {
        const QPoint p = ctx.toPixels(ctx.num(n, QStringLiteral("x")), ctx.num(n, QStringLiteral("y")));
        const int ms = std::max(200, ctx.integer(n, QStringLiteral("durationMs"), 800));
        const AdbResult r = ctx.shell(QStringLiteral("input swipe %1 %2 %1 %2 %3").arg(p.x()).arg(p.y()).arg(ms), 10000 + ms);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    if (t == QLatin1String("input.swipe")) {
        const QPoint a = ctx.toPixels(ctx.num(n, QStringLiteral("x1")), ctx.num(n, QStringLiteral("y1")));
        const QPoint b = ctx.toPixels(ctx.num(n, QStringLiteral("x2")), ctx.num(n, QStringLiteral("y2")));
        const int ms = std::max(50, ctx.integer(n, QStringLiteral("durationMs"), 300));
        const AdbResult r = ctx.shell(QStringLiteral("input swipe %1 %2 %3 %4 %5").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y()).arg(ms), 10000 + ms);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    if (t == QLatin1String("input.scroll")) {
        const QString dir = ctx.str(n, QStringLiteral("direction"));
        const double amount = std::clamp(ctx.num(n, QStringLiteral("amount"), 0.5), 0.05, 0.95);
        const int ms = std::max(50, ctx.integer(n, QStringLiteral("durationMs"), 300));
        QPoint a = ctx.toPixels(0.5, 0.5);
        QPoint b = a;
        const QSize s = ctx.screen;
        const int dx = static_cast<int>(amount * s.width() / 2);
        const int dy = static_cast<int>(amount * s.height() / 2);
        if (dir == QLatin1String("down")) {
            a.ry() += dy;
            b.ry() -= dy;
        } else if (dir == QLatin1String("up")) {
            a.ry() -= dy;
            b.ry() += dy;
        } else if (dir == QLatin1String("left")) {
            a.rx() += dx;
            b.rx() -= dx;
        } else {
            a.rx() -= dx;
            b.rx() += dx;
        }
        const AdbResult r = ctx.shell(QStringLiteral("input swipe %1 %2 %3 %4 %5").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y()).arg(ms), 10000 + ms);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    if (t == QLatin1String("input.text")) {
        const QString text = ctx.str(n, QStringLiteral("text"));
        if (ctx.flag(n, QStringLiteral("clearFirst"))) {
            ctx.shell(QStringLiteral("input keyevent KEYCODE_MOVE_END; input keyevent --longpress $(printf 'KEYCODE_DEL %.0s' $(seq 1 60))"), 15000);
        }
        auto dev = DeviceService::instance().device(ctx.deviceId);
        if (dev && DeviceService::instance().isMirroring(ctx.deviceId)) {
            QString copy = text;
            QMetaObject::invokeMethod(&DeviceService::instance(), [dev, copy]() mutable {
                if (dev) {
                    dev->postTextInput(copy);
                }
            }, Qt::BlockingQueuedConnection);
            return okPort(QStringLiteral("out"), QStringLiteral("typed via mirror"));
        }
        bool ascii = true;
        for (const QChar ch : text) {
            if (ch.unicode() > 0x7E || ch.unicode() < 0x20) {
                ascii = false;
            }
        }
        if (!ascii) {
            return fail(QStringLiteral("non-ASCII text needs an active mirror"));
        }
        const AdbResult r = ctx.shell(QStringLiteral("input text %1").arg(adb::shellQuote(adb::inputTextEscape(text))), 20000);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    if (t == QLatin1String("input.key") || t == QLatin1String("input.back") || t == QLatin1String("input.home") || t == QLatin1String("input.recent")) {
        QString key = t == QLatin1String("input.back") ? QStringLiteral("KEYCODE_BACK") : t == QLatin1String("input.home") ? QStringLiteral("KEYCODE_HOME") : t == QLatin1String("input.recent") ? QStringLiteral("KEYCODE_APP_SWITCH") : ctx.str(n, QStringLiteral("key"));
        if (!key.startsWith(QLatin1String("KEYCODE_")) && !key.at(0).isDigit()) {
            key = QStringLiteral("KEYCODE_") + key.toUpper();
        }
        const AdbResult r = ctx.shell(QStringLiteral("input keyevent %1").arg(adb::shellQuote(key)), 10000);
        return r.ok ? okPort(QStringLiteral("out"), key) : fail(r.error);
    }
    return fail(QStringLiteral("unknown input node %1").arg(t));
}

QString foregroundPackage(DeviceRunContext &ctx)
{
    const AdbResult r = ctx.shell(QStringLiteral("dumpsys activity activities 2>/dev/null | grep -E 'mResumedActivity|topResumedActivity|ResumedActivity' | head -1"), 10000);
    static const QRegularExpression re(QStringLiteral("([A-Za-z0-9_.]+)/"));
    const QRegularExpressionMatch m = re.match(r.stdOut);
    return m.hasMatch() ? m.captured(1) : QString();
}

NodeResult execApp(DeviceRunContext &ctx, const WorkflowNode &n)
{
    const QString t = n.type;
    const QString pkg = ctx.str(n, QStringLiteral("package"));
    if (t == QLatin1String("app.launch")) {
        const AdbResult r = ctx.shell(QStringLiteral("monkey -p %1 -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 && echo launched").arg(adb::shellQuote(pkg)), 20000);
        return r.stdOut.contains(QLatin1String("launched")) ? okPort(QStringLiteral("out")) : fail(r.error.isEmpty() ? QStringLiteral("could not launch %1").arg(pkg) : r.error);
    }
    if (t == QLatin1String("app.forceStop")) {
        const AdbResult r = ctx.shell(QStringLiteral("am force-stop %1").arg(adb::shellQuote(pkg)), 15000);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    if (t == QLatin1String("app.clearData")) {
        const AdbResult r = ctx.shell(QStringLiteral("pm clear %1").arg(adb::shellQuote(pkg)), 30000);
        return r.ok && r.stdOut.contains(QLatin1String("Success")) ? okPort(QStringLiteral("out")) : fail(r.combined().trimmed());
    }
    if (t == QLatin1String("app.install")) {
        QStringList args{ QStringLiteral("install") };
        if (ctx.flag(n, QStringLiteral("reinstall"), true)) {
            args << QStringLiteral("-r");
        }
        if (ctx.flag(n, QStringLiteral("grant"))) {
            args << QStringLiteral("-g");
        }
        args << QDir::toNativeSeparators(ctx.assetPath(ctx.str(n, QStringLiteral("apk"))));
        const AdbResult r = ctx.adb(args, 300000);
        return r.combined().contains(QLatin1String("Success")) ? okPort(QStringLiteral("out")) : fail(r.combined().trimmed().isEmpty() ? r.error : r.combined().trimmed());
    }
    if (t == QLatin1String("app.uninstall")) {
        const AdbResult r = ctx.adb({ QStringLiteral("uninstall"), pkg }, 60000);
        return r.combined().contains(QLatin1String("Success")) ? okPort(QStringLiteral("out")) : fail(r.combined().trimmed());
    }
    if (t == QLatin1String("app.isForeground")) {
        return okPort(foregroundPackage(ctx) == pkg ? QStringLiteral("true") : QStringLiteral("false"));
    }
    if (t == QLatin1String("app.waitFor")) {
        const int timeout = ctx.integer(n, QStringLiteral("timeoutMs"), 15000);
        const int interval = std::max(200, ctx.integer(n, QStringLiteral("intervalMs"), 1000));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeout && !ctx.cancelled()) {
            if (foregroundPackage(ctx) == pkg) {
                return okPort(QStringLiteral("found"));
            }
            QThread::msleep(static_cast<unsigned long>(interval));
        }
        return okPort(QStringLiteral("timeout"), QStringLiteral("%1 not in foreground after %2 ms").arg(pkg).arg(timeout));
    }
    return fail(QStringLiteral("unknown app node %1").arg(t));
}

NodeResult execScreen(DeviceRunContext &ctx, const WorkflowNode &n)
{
    const QString t = n.type;
    if (t == QLatin1String("screen.screenshot") || t == QLatin1String("log.screenshot")) {
        QString err;
        const QImage img = ctx.screenshot(&err);
        if (img.isNull()) {
            return fail(err);
        }
        const QString path = ctx.saveImage(img, ctx.str(n, QStringLiteral("tag")).isEmpty() ? QStringLiteral("shot") : ctx.str(n, QStringLiteral("tag")));
        NodeResult r = okPort(QStringLiteral("out"), path);
        return r;
    }
    if (t == QLatin1String("screen.findImage") || t == QLatin1String("screen.waitForImage") || t == QLatin1String("screen.tapImage")) {
        const QString imagePath = ctx.assetPath(ctx.str(n, QStringLiteral("image")));
        QImage needle(imagePath);
        if (needle.isNull()) {
            return fail(QStringLiteral("template image not found: %1").arg(imagePath));
        }
        const double threshold = std::clamp(ctx.num(n, QStringLiteral("threshold"), 0.85), 0.1, 1.0);
        const bool wait = t == QLatin1String("screen.waitForImage");
        const bool untilGone = wait && ctx.flag(n, QStringLiteral("untilGone"));
        const int timeout = wait ? ctx.integer(n, QStringLiteral("timeoutMs"), 15000) : 0;
        const int interval = std::max(200, ctx.integer(n, QStringLiteral("intervalMs"), 1000));
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            QString err;
            const QImage shot = ctx.screenshot(&err);
            if (shot.isNull()) {
                return fail(err);
            }
            const QRect region = ctx.regionFromParam(ctx.str(n, QStringLiteral("region")), shot.size());
            const QList<ImageMatch> matches = ImageMatcher::find(shot, needle, threshold, region, 1);
            const bool found = !matches.isEmpty();
            if (found && !untilGone) {
                const QString var = ctx.str(n, QStringLiteral("saveTo"));
                if (!var.isEmpty()) {
                    ctx.vars.insert(var, ctx.matchToVar(matches.first()));
                }
                if (t == QLatin1String("screen.tapImage")) {
                    const NodeResult tap = tapAt(ctx, matches.first().center().toPoint());
                    if (!tap.ok) {
                        return tap;
                    }
                }
                return okPort(QStringLiteral("found"), QStringLiteral("score %1 at %2,%3").arg(matches.first().score, 0, 'f', 3).arg(matches.first().center().x()).arg(matches.first().center().y()));
            }
            if (!found && untilGone) {
                return okPort(QStringLiteral("found"), QStringLiteral("image gone"));
            }
            if (!wait || timer.elapsed() >= timeout || ctx.cancelled()) {
                break;
            }
            QThread::msleep(static_cast<unsigned long>(interval));
        }
        return okPort(QStringLiteral("timeout"), wait ? QStringLiteral("not %1 after %2 ms").arg(untilGone ? QStringLiteral("gone") : QStringLiteral("found")).arg(timeout) : QStringLiteral("not found"));
    }
    if (t == QLatin1String("screen.ocr") || t == QLatin1String("screen.waitForText") || t == QLatin1String("screen.tapText")) {
        if (!OcrProvider::available()) {
            return fail(QStringLiteral("OCR not available (%1)").arg(OcrProvider::backendName()));
        }
        const bool wait = t == QLatin1String("screen.waitForText");
        const int timeout = wait ? ctx.integer(n, QStringLiteral("timeoutMs"), 15000) : 0;
        const int interval = std::max(300, ctx.integer(n, QStringLiteral("intervalMs"), 1500));
        const QString needle = ctx.str(n, QStringLiteral("text"));
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            QString err;
            QImage shot = ctx.screenshot(&err);
            if (shot.isNull()) {
                return fail(err);
            }
            const QRect region = ctx.regionFromParam(ctx.str(n, QStringLiteral("region")), shot.size());
            QPoint offset;
            if (region.isValid()) {
                shot = shot.copy(region);
                offset = region.topLeft();
            }
            const OcrResult ocr = OcrProvider::recognize(shot);
            if (!ocr.ok()) {
                return fail(ocr.error);
            }
            if (t == QLatin1String("screen.ocr")) {
                ctx.vars.insert(ctx.str(n, QStringLiteral("saveTo")).isEmpty() ? QStringLiteral("text") : ctx.str(n, QStringLiteral("saveTo")), ocr.text);
                return okPort(QStringLiteral("out"), ocr.text.left(200));
            }
            QRect r = ocr.find(needle);
            if (r.isValid()) {
                r.translate(offset);
                const QString var = ctx.str(n, QStringLiteral("saveTo"));
                if (!var.isEmpty()) {
                    ctx.vars.insert(var, ctx.rectToVar(r));
                }
                if (t == QLatin1String("screen.tapText")) {
                    const NodeResult tap = tapAt(ctx, r.center());
                    if (!tap.ok) {
                        return tap;
                    }
                }
                return okPort(QStringLiteral("found"), QStringLiteral("'%1' at %2,%3").arg(needle).arg(r.center().x()).arg(r.center().y()));
            }
            if (!wait || timer.elapsed() >= timeout || ctx.cancelled()) {
                break;
            }
            QThread::msleep(static_cast<unsigned long>(interval));
        }
        return okPort(QStringLiteral("timeout"), QStringLiteral("text '%1' not found").arg(needle));
    }
    if (t == QLatin1String("ui.dump") || t == QLatin1String("ui.waitForElement") || t == QLatin1String("ui.tapElement") || t == QLatin1String("ui.exists")) {
        const bool wait = t == QLatin1String("ui.waitForElement");
        const int timeout = wait ? ctx.integer(n, QStringLiteral("timeoutMs"), 15000) : 0;
        const int interval = std::max(300, ctx.integer(n, QStringLiteral("intervalMs"), 1000));
        const UiSelector selector = UiSelector::parse(ctx.str(n, QStringLiteral("selector")));
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            const AdbResult r = ctx.shell(UiHierarchy::dumpScript(), 20000);
            if (!r.ok) {
                return fail(r.error.isEmpty() ? QStringLiteral("uiautomator dump failed") : r.error);
            }
            if (t == QLatin1String("ui.dump")) {
                ctx.vars.insert(ctx.str(n, QStringLiteral("saveTo")), r.stdOut);
                return okPort(QStringLiteral("out"), QStringLiteral("%1 chars").arg(r.stdOut.size()));
            }
            const QList<UiNode> nodes = UiHierarchy::parse(r.stdOut);
            const QList<UiNode> hits = UiHierarchy::find(nodes, selector);
            if (!hits.isEmpty()) {
                const UiNode &hit = hits.first();
                const QString var = ctx.str(n, QStringLiteral("saveTo"));
                if (!var.isEmpty()) {
                    ctx.vars.insert(var, ctx.rectToVar(hit.bounds));
                }
                if (t == QLatin1String("ui.exists")) {
                    return okPort(QStringLiteral("true"));
                }
                if (t == QLatin1String("ui.tapElement")) {
                    const NodeResult tap = tapAt(ctx, hit.center());
                    if (!tap.ok) {
                        return tap;
                    }
                }
                return okPort(QStringLiteral("found"), QStringLiteral("%1 at %2,%3").arg(selector.toString()).arg(hit.center().x()).arg(hit.center().y()));
            }
            if (t == QLatin1String("ui.exists")) {
                return okPort(QStringLiteral("false"));
            }
            if (!wait || timer.elapsed() >= timeout || ctx.cancelled()) {
                break;
            }
            QThread::msleep(static_cast<unsigned long>(interval));
        }
        return okPort(QStringLiteral("timeout"), QStringLiteral("element '%1' not found").arg(selector.toString()));
    }
    return fail(QStringLiteral("unknown screen node %1").arg(t));
}

NodeResult execFile(DeviceRunContext &ctx, const WorkflowNode &n)
{
    const QString t = n.type;
    const QString remote = ctx.str(n, QStringLiteral("remote"));
    if (t == QLatin1String("file.push")) {
        const QString local = ctx.assetPath(ctx.str(n, QStringLiteral("local")));
        ctx.shell(QStringLiteral("mkdir -p %1").arg(adb::shellQuote(remote)), 8000);
        const AdbResult r = ctx.adb({ QStringLiteral("push"), QDir::toNativeSeparators(local), remote }, 600000);
        return r.ok ? okPort(QStringLiteral("out"), r.stdOut.trimmed().section(QLatin1Char('\n'), -1)) : fail(r.error);
    }
    if (t == QLatin1String("file.pull")) {
        QString dir = ctx.str(n, QStringLiteral("localDir"));
        if (dir.isEmpty()) {
            dir = ctx.deviceDir;
        }
        QDir().mkpath(dir);
        const AdbResult r = ctx.adb({ QStringLiteral("pull"), remote, QDir::toNativeSeparators(dir) }, 600000);
        return r.ok ? okPort(QStringLiteral("out"), dir) : fail(r.error);
    }
    if (t == QLatin1String("file.exists")) {
        const AdbResult r = ctx.shell(QStringLiteral("test -e %1 && echo yes || echo no").arg(adb::shellQuote(remote)), 8000);
        return okPort(r.stdOut.contains(QLatin1String("yes")) ? QStringLiteral("true") : QStringLiteral("false"));
    }
    if (t == QLatin1String("file.delete")) {
        if (remote.trimmed().isEmpty() || remote.trimmed() == QLatin1String("/") || remote.trimmed() == QLatin1String("/sdcard")) {
            return fail(QStringLiteral("refusing to delete a root directory"));
        }
        const AdbResult r = ctx.shell(QStringLiteral("rm -rf %1 && echo deleted").arg(adb::shellQuote(remote)), 60000);
        return r.ok ? okPort(QStringLiteral("out")) : fail(r.error);
    }
    return fail(QStringLiteral("unknown file node %1").arg(t));
}

NodeResult executeNode(DeviceRunContext &ctx, const WorkflowNode &n, AutomationRun *run)
{
    const QString t = n.type;
    // ---- flow ----
    if (t == QLatin1String("flow.start")) {
        return okPort(QStringLiteral("out"));
    }
    if (t == QLatin1String("flow.end")) {
        return okPort(QString());
    }
    if (t == QLatin1String("flow.fail")) {
        return fail(ctx.str(n, QStringLiteral("message")));
    }
    if (t == QLatin1String("flow.runWorkflow")) {
        return fail(QStringLiteral("handled by the interpreter"));
    }
    // ---- device ----
    if (t == QLatin1String("device.property")) {
        const DeviceRecord rec = DeviceRegistry::instance().get(ctx.deviceId);
        const QString prop = ctx.str(n, QStringLiteral("property"));
        QVariant v;
        if (prop == QLatin1String("id")) {
            v = rec.id.isEmpty() ? ctx.deviceId : rec.id;
        } else if (prop == QLatin1String("number")) {
            v = rec.number;
        } else if (prop == QLatin1String("name")) {
            v = rec.displayName();
        } else if (prop == QLatin1String("model")) {
            v = rec.model;
        } else if (prop == QLatin1String("ip")) {
            v = rec.host();
        } else if (prop == QLatin1String("group")) {
            v = rec.group;
        } else if (prop == QLatin1String("battery")) {
            v = rec.battery;
        } else if (prop == QLatin1String("androidVersion")) {
            v = rec.androidVersion;
        } else if (prop == QLatin1String("serial")) {
            v = rec.hwSerial;
        } else if (prop == QLatin1String("screenWidth")) {
            v = rec.screenSize.width();
        } else if (prop == QLatin1String("screenHeight")) {
            v = rec.screenSize.height();
        }
        ctx.vars.insert(ctx.str(n, QStringLiteral("variable")), v);
        return okPort(QStringLiteral("out"), Expression::displayValue(v));
    }
    if (t == QLatin1String("device.wake")) {
        const AdbResult r = ctx.shell(QStringLiteral("input keyevent KEYCODE_WAKEUP; dumpsys window policy 2>/dev/null | grep -E 'isKeyguardShowing|mShowingLockscreen|showing=' | head -2"), 10000);
        const adb::ScreenState s = adb::parseScreenState(r.stdOut);
        return r.ok ? okPort(QStringLiteral("out"), s.locked ? QStringLiteral("awake but locked") : QStringLiteral("awake")) : fail(r.error);
    }
    if (t == QLatin1String("device.keepAwake")) {
        QMetaObject::invokeMethod(&KeepAwakeManager::instance(), [id = ctx.deviceId]() { KeepAwakeManager::instance().applyPolicy(id); }, Qt::QueuedConnection);
        return okPort(QStringLiteral("out"));
    }
    if (t.startsWith(QLatin1String("input."))) {
        return execInput(ctx, n);
    }
    if (t.startsWith(QLatin1String("app."))) {
        return execApp(ctx, n);
    }
    // ---- timing ----
    if (t == QLatin1String("time.wait") || t == QLatin1String("time.waitRandom")) {
        int ms = ctx.integer(n, QStringLiteral("ms"), 1000);
        if (t == QLatin1String("time.waitRandom")) {
            const int lo = std::max(0, ctx.integer(n, QStringLiteral("minMs"), 500));
            const int hi = std::max(lo, ctx.integer(n, QStringLiteral("maxMs"), 1500));
            ms = lo + QRandomGenerator::global()->bounded(hi - lo + 1);
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms && !ctx.cancelled()) {
            QThread::msleep(static_cast<unsigned long>(std::min<qint64>(qint64(100), qint64(ms) - timer.elapsed())));
            ctx.waitWhilePaused();
        }
        return okPort(QStringLiteral("out"), QStringLiteral("%1 ms").arg(ms));
    }
    // ---- logic ----
    if (t == QLatin1String("logic.if")) {
        QString err;
        const bool v = Expression::evaluate(n.params.value(QStringLiteral("condition")).toString(), ctx.vars, &err);
        return okPort(v ? QStringLiteral("true") : QStringLiteral("false"), Expression::substitute(n.params.value(QStringLiteral("condition")).toString(), ctx.vars));
    }
    if (t == QLatin1String("logic.switch")) {
        const QString v = Expression::displayValue(Expression::value(n.params.value(QStringLiteral("value")).toString(), ctx.vars));
        const QStringList cases = ctx.str(n, QStringLiteral("cases")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &c : cases) {
            if (c.trimmed().compare(v, Qt::CaseInsensitive) == 0) {
                return okPort(QStringLiteral("case:") + c.trimmed(), v);
            }
        }
        return okPort(QStringLiteral("default"), v);
    }
    // ---- variables ----
    if (t == QLatin1String("var.set")) {
        const QVariant v = Expression::value(n.params.value(QStringLiteral("value")).toString(), ctx.vars);
        ctx.vars.insert(ctx.str(n, QStringLiteral("name")), v);
        return okPort(QStringLiteral("out"), QStringLiteral("%1 = %2").arg(ctx.str(n, QStringLiteral("name")), Expression::displayValue(v)));
    }
    if (t == QLatin1String("var.increment")) {
        const QString name = ctx.str(n, QStringLiteral("name"));
        const double v = ctx.vars.value(name).toDouble() + ctx.num(n, QStringLiteral("by"), 1);
        ctx.vars.insert(name, v);
        return okPort(QStringLiteral("out"), QStringLiteral("%1 = %2").arg(name).arg(v));
    }
    if (t == QLatin1String("var.listAppend")) {
        const QString name = ctx.str(n, QStringLiteral("name"));
        QVariantList list = ctx.vars.value(name).toList();
        list.append(Expression::value(n.params.value(QStringLiteral("value")).toString(), ctx.vars));
        ctx.vars.insert(name, list);
        return okPort(QStringLiteral("out"), QStringLiteral("%1 items").arg(list.size()));
    }
    // ---- adb ----
    if (t == QLatin1String("adb.shell")) {
        const AdbResult r = ctx.shell(ctx.str(n, QStringLiteral("script")), n.timeoutMs > 0 ? n.timeoutMs : 30000);
        const QString var = ctx.str(n, QStringLiteral("saveTo"));
        if (!var.isEmpty()) {
            ctx.vars.insert(var, r.stdOut.trimmed());
        }
        if (!r.ok && ctx.flag(n, QStringLiteral("failOnError"), true)) {
            return fail(r.error.isEmpty() ? r.combined().trimmed() : r.error);
        }
        return okPort(QStringLiteral("out"), r.stdOut.trimmed().left(200));
    }
    if (t == QLatin1String("adb.exec")) {
        const QStringList args = ctx.str(n, QStringLiteral("args")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const AdbResult r = ctx.adb(args, n.timeoutMs > 0 ? n.timeoutMs : 120000);
        const QString var = ctx.str(n, QStringLiteral("saveTo"));
        if (!var.isEmpty()) {
            ctx.vars.insert(var, r.combined().trimmed());
        }
        return r.ok ? okPort(QStringLiteral("out"), r.combined().trimmed().left(200)) : fail(r.error);
    }
    if (t == QLatin1String("adb.getprop")) {
        const AdbResult r = ctx.shell(QStringLiteral("getprop %1").arg(adb::shellQuote(ctx.str(n, QStringLiteral("property")))), 10000);
        ctx.vars.insert(ctx.str(n, QStringLiteral("saveTo")), r.stdOut.trimmed());
        return r.ok ? okPort(QStringLiteral("out"), r.stdOut.trimmed()) : fail(r.error);
    }
    if (t.startsWith(QLatin1String("screen.")) || t.startsWith(QLatin1String("ui.")) || t == QLatin1String("log.screenshot")) {
        return execScreen(ctx, n);
    }
    if (t.startsWith(QLatin1String("file."))) {
        return execFile(ctx, n);
    }
    // ---- logging ----
    if (t == QLatin1String("log.message")) {
        const QString level = ctx.str(n, QStringLiteral("level"));
        const QString msg = ctx.str(n, QStringLiteral("message"));
        ActivityLog::instance().post(level == QLatin1String("error") ? ActivityEntry::Error : level == QLatin1String("warning") ? ActivityEntry::Warning : ActivityEntry::Info, ActivityEntry::Automation,
                                     QStringLiteral("[%1] %2").arg(run->name(), msg), ctx.deviceId);
        return okPort(QStringLiteral("out"), msg);
    }
    if (t == QLatin1String("log.errorReport")) {
        QString err;
        const QImage img = ctx.screenshot(&err);
        QString path;
        if (!img.isNull()) {
            path = ctx.saveImage(img, QStringLiteral("report"));
        }
        const AdbResult ui = ctx.shell(UiHierarchy::dumpScript(), 20000);
        QDir().mkpath(ctx.deviceDir);
        QFile f(QStringLiteral("%1/report-step%2.txt").arg(ctx.deviceDir).arg(ctx.step));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QJsonObject vars = QJsonObject::fromVariantMap(ctx.vars);
            f.write(QStringLiteral("%1\n\nVariables:\n%2\n\nUI hierarchy:\n%3\n").arg(ctx.str(n, QStringLiteral("message")), QString::fromUtf8(QJsonDocument(vars).toJson()), ui.stdOut).toUtf8());
        }
        NodeResult r = okPort(QStringLiteral("out"), path);
        return r;
    }
    return fail(QStringLiteral("unsupported node type '%1'").arg(t));
}

// ---------------------------------------------------------------- interpreter

bool executeWorkflow(DeviceRunContext &ctx, const Workflow &wf, AutomationRun *run, QString &errorOut);

bool executeWorkflow(DeviceRunContext &ctx, const Workflow &wf, AutomationRun *run, QString &errorOut)
{
    QString current = wf.startNodeId();
    if (current.isEmpty()) {
        errorOut = QStringLiteral("workflow has no start node");
        return false;
    }
    const int maxSteps = 20000;
    int guard = 0;
    while (!current.isEmpty()) {
        if (ctx.cancelled()) {
            errorOut = QStringLiteral("cancelled");
            return false;
        }
        ctx.waitWhilePaused();
        if (++guard > maxSteps) {
            errorOut = QStringLiteral("step limit reached (%1)").arg(maxSteps);
            return false;
        }
        const WorkflowNode node = wf.node(current);
        const NodeSpec spec = NodeCatalog::spec(node.type);
        const QString title = node.title.isEmpty() ? spec.title : node.title;
        ++ctx.step;
        run->reportProgress(ctx.deviceId, node.id, title, ctx.step);

        if (node.disabled) {
            current = wf.nextNode(node.id, QStringLiteral("out"));
            continue;
        }

        // ---- loops ----
        if (node.type == QLatin1String("logic.loop") || node.type == QLatin1String("logic.while")) {
            LoopFrame *frame = nullptr;
            for (LoopFrame &f : ctx.loops) {
                if (f.nodeId == node.id) {
                    frame = &f;
                }
            }
            if (!frame) {
                LoopFrame f;
                f.nodeId = node.id;
                f.limit = node.type == QLatin1String("logic.loop") ? ctx.integer(node, QStringLiteral("count"), 3) : ctx.integer(node, QStringLiteral("maxIterations"), 1000);
                ctx.loops.append(f);
                frame = &ctx.loops.last();
            }
            bool again = false;
            if (node.type == QLatin1String("logic.loop")) {
                again = frame->iteration < frame->limit;
                ctx.vars.insert(ctx.str(node, QStringLiteral("indexVariable")).isEmpty() ? QStringLiteral("i") : ctx.str(node, QStringLiteral("indexVariable")), frame->iteration);
            } else {
                again = frame->iteration < frame->limit && Expression::evaluate(node.params.value(QStringLiteral("condition")).toString(), ctx.vars);
            }
            if (again) {
                frame->iteration += 1;
                ctx.log(title, QStringLiteral("info"), QStringLiteral("iteration %1").arg(frame->iteration), QString(), 0);
                current = wf.nextNode(node.id, QStringLiteral("body"));
                if (current.isEmpty()) {
                    // empty body: keep looping until done
                    continue;
                }
            } else {
                ctx.loops.removeLast();
                current = wf.nextNode(node.id, QStringLiteral("done"));
            }
            continue;
        }
        if (node.type == QLatin1String("logic.break") || node.type == QLatin1String("logic.continue")) {
            if (ctx.loops.isEmpty()) {
                current = wf.nextNode(node.id, QStringLiteral("out"));
                continue;
            }
            const LoopFrame frame = ctx.loops.last();
            if (node.type == QLatin1String("logic.break")) {
                ctx.loops.removeLast();
                current = wf.nextNode(frame.nodeId, QStringLiteral("done"));
            } else {
                current = frame.nodeId;
            }
            continue;
        }
        if (node.type == QLatin1String("flow.runWorkflow")) {
            if (ctx.depth >= 5) {
                errorOut = QStringLiteral("sub-workflow depth limit");
                return false;
            }
            bool found = false;
            const Workflow sub = WorkflowEngine::loadWorkflow(ctx.str(node, QStringLiteral("workflow")), &found);
            if (!found) {
                errorOut = QStringLiteral("workflow '%1' not found").arg(ctx.str(node, QStringLiteral("workflow")));
                return false;
            }
            ++ctx.depth;
            QList<LoopFrame> savedLoops = ctx.loops;
            ctx.loops.clear();
            const bool ok = executeWorkflow(ctx, sub, run, errorOut);
            ctx.loops = savedLoops;
            --ctx.depth;
            if (!ok) {
                return false;
            }
            current = wf.nextNode(node.id, QStringLiteral("out"));
            continue;
        }

        // ---- regular node with retry / timeout / onFailure ----
        NodeResult result;
        QElapsedTimer timer;
        timer.start();
        int attempt = 0;
        for (;;) {
            ++attempt;
            result = executeNode(ctx, node, run);
            if (result.ok || attempt > node.retryCount || ctx.cancelled()) {
                break;
            }
            ctx.log(title, QStringLiteral("info"), QStringLiteral("retry %1/%2 after %3 ms: %4").arg(attempt).arg(node.retryCount).arg(node.retryDelayMs).arg(result.error), QString(), timer.elapsed());
            QThread::msleep(static_cast<unsigned long>(std::max(0, node.retryDelayMs)));
        }
        const qint64 ms = timer.elapsed();
        if (result.ok) {
            ctx.log(title, QStringLiteral("ok"), result.message, QString(), ms);
        } else {
            QString shot;
            if (node.onFailure == QLatin1String("fail") && FarmSettings::instance().errorScreenshots()) {
                QString err;
                const QImage img = ctx.screenshot(&err);
                if (!img.isNull()) {
                    shot = ctx.saveImage(img, QStringLiteral("error"));
                }
            }
            ctx.log(title, QStringLiteral("failed"), result.message, result.error, ms, shot);
            if (node.onFailure == QLatin1String("continue")) {
                current = wf.nextNode(node.id, QStringLiteral("out"));
                if (current.isEmpty() && !spec.outputs.isEmpty()) {
                    current = wf.nextNode(node.id, spec.outputs.first());
                }
                continue;
            }
            errorOut = QStringLiteral("%1: %2").arg(title, result.error);
            if (!shot.isEmpty()) {
                errorOut += QStringLiteral(" [screenshot %1]").arg(shot);
            }
            return false;
        }
        if (result.port.isEmpty()) {
            return true;    // End node
        }
        current = wf.nextNode(node.id, result.port);
        // Reaching the end of a loop body returns to the loop node.
        if (current.isEmpty() && !ctx.loops.isEmpty()) {
            current = ctx.loops.last().nodeId;
        }
    }
    return true;
}

} // namespace

// =====================================================================
//  AutomationRun
// =====================================================================

AutomationRun::AutomationRun(const Workflow &workflow, const QStringList &targets, int concurrency, const QString &triggeredBy, QObject *parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_workflow(workflow)
    , m_targets(targets)
    , m_concurrency(std::clamp(concurrency, 1, 64))
    , m_triggeredBy(triggeredBy)
{
    qRegisterMetaType<farm::AutomationRun::Status>("farm::AutomationRun::Status");
    qRegisterMetaType<farm::JobLogRow>("farm::JobLogRow");
    for (const QString &id : targets) {
        DeviceProgress p;
        p.id = id;
        m_progress.insert(id, p);
    }
    QString safeName = workflow.name;
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.\\-]")), QStringLiteral("_"));
    m_runDir = QStringLiteral("%1/%2_%3_%4").arg(FarmSettings::instance().automationRunsDirectory(), QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")), safeName.left(40), m_id.left(8));
}

AutomationRun::~AutomationRun()
{
    m_token.cancel();
}

QString AutomationRun::statusName(Status s)
{
    switch (s) {
    case Pending:
        return QStringLiteral("Pending");
    case Running:
        return QStringLiteral("Running");
    case Paused:
        return QStringLiteral("Paused");
    case Completed:
        return QStringLiteral("Completed");
    case Failed:
        return QStringLiteral("Failed");
    case Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QString();
}

AutomationRun::DeviceProgress AutomationRun::progress(const QString &deviceId) const
{
    QMutexLocker lock(&m_mutex);
    return m_progress.value(deviceId);
}

QList<AutomationRun::DeviceProgress> AutomationRun::allProgress() const
{
    QMutexLocker lock(&m_mutex);
    QList<DeviceProgress> list;
    for (const QString &id : m_targets) {
        list << m_progress.value(id);
    }
    return list;
}

int AutomationRun::succeeded() const
{
    QMutexLocker lock(&m_mutex);
    int n = 0;
    for (const DeviceProgress &p : m_progress) {
        n += p.status == QLatin1String("ok") ? 1 : 0;
    }
    return n;
}

int AutomationRun::failed() const
{
    QMutexLocker lock(&m_mutex);
    int n = 0;
    for (const DeviceProgress &p : m_progress) {
        n += p.status == QLatin1String("failed") ? 1 : 0;
    }
    return n;
}

int AutomationRun::running() const
{
    QMutexLocker lock(&m_mutex);
    int n = 0;
    for (const DeviceProgress &p : m_progress) {
        n += p.status == QLatin1String("running") ? 1 : 0;
    }
    return n;
}

int AutomationRun::queued() const
{
    QMutexLocker lock(&m_mutex);
    int n = 0;
    for (const DeviceProgress &p : m_progress) {
        n += p.status == QLatin1String("queued") ? 1 : 0;
    }
    return n;
}

int AutomationRun::percent() const
{
    const int t = total();
    if (t == 0) {
        return 100;
    }
    return (t - queued() - running()) * 100 / t;
}

QString AutomationRun::summary() const
{
    return QStringLiteral("%1 / %2 done · %3 ok · %4 failed · %5 running · %6 queued").arg(total() - queued() - running()).arg(total()).arg(succeeded()).arg(failed()).arg(running()).arg(queued());
}

QList<JobLogRow> AutomationRun::logs() const
{
    QMutexLocker lock(&m_mutex);
    return m_logs;
}

QStringList AutomationRun::failedIds() const
{
    QMutexLocker lock(&m_mutex);
    QStringList list;
    for (const QString &id : m_targets) {
        if (m_progress.value(id).status == QLatin1String("failed") || m_progress.value(id).status == QLatin1String("cancelled")) {
            list << id;
        }
    }
    return list;
}

void AutomationRun::setStatus(Status s)
{
    if (m_status == s) {
        return;
    }
    m_status = s;
    emit statusChanged(s);
    if (s == Completed || s == Failed || s == Cancelled) {
        m_finishedAt = QDateTime::currentDateTime();
        persistRun();
        writeLogsJson();
        ActivityLog::instance().post(s == Completed ? ActivityEntry::Info : ActivityEntry::Warning, ActivityEntry::Automation, QStringLiteral("Workflow '%1' %2: %3").arg(m_workflow.name, statusName(s).toLower(), summary()));
        emit finished(s);
    }
}

void AutomationRun::persistRun()
{
    JobRunRow row;
    row.id = m_id;
    row.kind = QStringLiteral("workflow");
    row.name = m_workflow.name;
    row.workflowId = m_workflow.id;
    row.started = m_startedAt;
    row.finished = m_finishedAt;
    row.status = statusName(m_status);
    row.total = total();
    row.succeeded = succeeded();
    row.failed = failed();
    QJsonObject o;
    o[QStringLiteral("targets")] = QJsonArray::fromStringList(m_targets);
    o[QStringLiteral("concurrency")] = m_concurrency;
    o[QStringLiteral("triggeredBy")] = m_triggeredBy;
    o[QStringLiteral("runDirectory")] = m_runDir;
    row.json = QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    RunRepository::saveRun(row);
}

void AutomationRun::writeLogsJson()
{
    QDir().mkpath(m_runDir);
    QJsonArray arr;
    for (const JobLogRow &l : logs()) {
        QJsonObject o;
        o[QStringLiteral("time")] = l.time.toString(Qt::ISODateWithMs);
        o[QStringLiteral("device")] = l.device;
        o[QStringLiteral("step")] = l.step;
        o[QStringLiteral("status")] = l.status;
        o[QStringLiteral("durationMs")] = static_cast<double>(l.durationMs);
        o[QStringLiteral("message")] = l.message;
        o[QStringLiteral("error")] = l.error;
        o[QStringLiteral("screenshot")] = l.screenshot;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("run")] = m_id;
    root[QStringLiteral("workflow")] = m_workflow.name;
    root[QStringLiteral("status")] = statusName(m_status);
    root[QStringLiteral("started")] = m_startedAt.toString(Qt::ISODate);
    root[QStringLiteral("finished")] = m_finishedAt.toString(Qt::ISODate);
    root[QStringLiteral("summary")] = summary();
    root[QStringLiteral("logs")] = arr;
    QFile f(m_runDir + QStringLiteral("/logs.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void AutomationRun::start()
{
    if (m_status == Running) {
        return;
    }
    if (!m_startedAt.isValid()) {
        m_startedAt = QDateTime::currentDateTime();
        QDir().mkpath(m_runDir);
        QFile wf(m_runDir + QStringLiteral("/workflow.json"));
        if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            wf.write(m_workflow.toJsonText().toUtf8());
        }
        persistRun();
        ActivityLog::instance().info(ActivityEntry::Automation, QStringLiteral("Workflow '%1' started on %2 device(s) (%3)").arg(m_workflow.name).arg(total()).arg(m_triggeredBy));
        for (const QString &id : m_targets) {
            DeviceRegistry::instance().updateRuntime(id, [](DeviceRecord &r) { r.automationRunning = true; });
        }
    }
    m_token.reset();
    m_stopRequested = false;
    m_paused = false;
    setStatus(Running);
    pump();
}

void AutomationRun::pause()
{
    if (m_status == Running) {
        m_paused = true;
        setStatus(Paused);
    }
}

void AutomationRun::resume()
{
    if (m_status == Paused) {
        m_paused = false;
        setStatus(Running);
        pump();
    }
}

void AutomationRun::stop()
{
    if (m_status != Running && m_status != Paused) {
        return;
    }
    m_stopRequested = true;
    m_paused = false;
    {
        QMutexLocker lock(&m_mutex);
        for (DeviceProgress &p : m_progress) {
            if (p.status == QLatin1String("queued")) {
                p.status = QStringLiteral("cancelled");
            }
        }
    }
    m_token.cancel();
    if (running() == 0) {
        setStatus(Cancelled);
    } else {
        m_status = Cancelled;
        emit statusChanged(Cancelled);
    }
}

void AutomationRun::cancel()
{
    stop();
}

void AutomationRun::retryFailed()
{
    bool any = false;
    {
        QMutexLocker lock(&m_mutex);
        for (DeviceProgress &p : m_progress) {
            if (p.status == QLatin1String("failed") || p.status == QLatin1String("cancelled")) {
                p.status = QStringLiteral("queued");
                p.error.clear();
                p.errorScreenshot.clear();
                p.steps = 0;
                any = true;
            }
        }
    }
    if (!any) {
        return;
    }
    m_finishedAt = QDateTime();
    m_token.reset();
    m_stopRequested = false;
    for (const QString &id : m_targets) {
        emit deviceChanged(id);
    }
    setStatus(Running);
    pump();
}

void AutomationRun::pump()
{
    if (m_status != Running) {
        if (m_status == Cancelled && running() == 0 && !m_finishedAt.isValid()) {
            m_finishedAt = QDateTime::currentDateTime();
            persistRun();
            writeLogsJson();
            for (const QString &id : m_targets) {
                DeviceRegistry::instance().updateRuntime(id, [](DeviceRecord &r) { r.automationRunning = false; });
            }
            emit finished(Cancelled);
        }
        return;
    }
    while (running() < m_concurrency && !m_stopRequested) {
        QString next;
        {
            QMutexLocker lock(&m_mutex);
            for (const QString &id : m_targets) {
                if (m_progress[id].status == QLatin1String("queued")) {
                    next = id;
                    m_progress[id].status = QStringLiteral("running");
                    m_progress[id].startedMs = QDateTime::currentMSecsSinceEpoch();
                    m_progress[id].attempts += 1;
                    break;
                }
            }
        }
        if (next.isEmpty()) {
            break;
        }
        emit deviceChanged(next);
        const Workflow wf = m_workflow;
        const QString runDir = m_runDir;
        QPointer<AutomationRun> self(this);
        TaskExecutor::instance().run(QStringLiteral("automation"), [self, wf, next, runDir]() {
            if (!self) {
                return;
            }
            DeviceRunContext ctx;
            ctx.run = self.data();
            ctx.deviceId = next;
            ctx.vars = wf.variables;
            ctx.deviceDir = runDir + QLatin1Char('/') + DeviceCommands::fileSafeId(next);
            DeviceRecord rec = DeviceRegistry::instance().get(next);
            if (rec.id.isEmpty()) {
                rec.id = next;    // unknown to the registry (tests, ad-hoc ids)
            }
            QVariantMap device;
            device[QStringLiteral("id")] = rec.id;
            device[QStringLiteral("number")] = rec.number;
            device[QStringLiteral("name")] = rec.displayName();
            device[QStringLiteral("model")] = rec.model;
            device[QStringLiteral("ip")] = rec.host();
            device[QStringLiteral("group")] = rec.group;
            device[QStringLiteral("battery")] = rec.battery;
            ctx.vars.insert(QStringLiteral("device"), device);
            ctx.screen = rec.screenSize;
            QString error;
            bool ok = false;
            if (!self->isCancelled()) {
                ok = executeWorkflow(ctx, wf, self.data(), error);
            } else {
                error = QStringLiteral("cancelled");
            }
            QString shot;
            const int bracket = error.indexOf(QLatin1String("[screenshot "));
            if (bracket >= 0) {
                shot = error.mid(bracket + 12).chopped(1);
            }
            if (self) {
                self->reportDeviceFinished(next, ok, error, shot);
            }
        });
    }
    if (running() == 0 && queued() == 0) {
        setStatus(failed() == 0 ? Completed : (succeeded() == 0 ? Failed : Completed));
        for (const QString &id : m_targets) {
            DeviceRegistry::instance().updateRuntime(id, [](DeviceRecord &r) { r.automationRunning = false; });
        }
    }
}

void AutomationRun::reportProgress(const QString &deviceId, const QString &nodeId, const QString &title, int steps)
{
    {
        QMutexLocker lock(&m_mutex);
        DeviceProgress &p = m_progress[deviceId];
        p.currentNode = nodeId;
        p.currentTitle = title;
        p.steps = steps;
    }
    QMetaObject::invokeMethod(this, [this, deviceId]() { emit deviceChanged(deviceId); }, Qt::QueuedConnection);
}

void AutomationRun::reportLog(const JobLogRow &row)
{
    {
        QMutexLocker lock(&m_mutex);
        m_logs.append(row);
    }
    RunRepository::appendLog(row);    // worker thread has its own DB connection
    QMetaObject::invokeMethod(this, [this, row]() { emit logAppended(row); }, Qt::QueuedConnection);
}

void AutomationRun::reportDeviceFinished(const QString &deviceId, bool ok, const QString &error, const QString &screenshot)
{
    QMetaObject::invokeMethod(this, [this, deviceId, ok, error, screenshot]() {
        {
            QMutexLocker lock(&m_mutex);
            DeviceProgress &p = m_progress[deviceId];
            p.status = ok ? QStringLiteral("ok") : (m_token.isCancelled() ? QStringLiteral("cancelled") : QStringLiteral("failed"));
            p.error = error;
            p.errorScreenshot = screenshot;
            p.finishedMs = QDateTime::currentMSecsSinceEpoch();
        }
        DeviceRegistry::instance().updateRuntime(deviceId, [](DeviceRecord &r) { r.automationRunning = false; });
        if (!ok) {
            FarmLog::instance().warning(QStringLiteral("automation"), QStringLiteral("'%1' failed: %2").arg(m_workflow.name, error), deviceId);
        }
        emit deviceChanged(deviceId);
        pump();
    }, Qt::QueuedConnection);
}

// =====================================================================
//  WorkflowEngine
// =====================================================================

WorkflowEngine &WorkflowEngine::instance()
{
    static WorkflowEngine engine;
    return engine;
}

WorkflowEngine::WorkflowEngine(QObject *parent)
    : QObject(parent)
{
}

AutomationRun *WorkflowEngine::start(const Workflow &workflow, const QStringList &targets, int concurrency, const QString &triggeredBy)
{
    auto *run = new AutomationRun(workflow, targets, concurrency, triggeredBy, this);
    m_runs.prepend(run);
    connect(run, &AutomationRun::statusChanged, this, [this](AutomationRun::Status) { emit runsChanged(); });
    connect(run, &AutomationRun::deviceChanged, this, [this](const QString &) { emit runsChanged(); });
    while (m_runs.size() > 50) {
        AutomationRun *old = m_runs.last();
        if (old->status() == AutomationRun::Running || old->status() == AutomationRun::Paused) {
            break;
        }
        m_runs.removeLast();
        emit runRemoved(old->id());
        old->deleteLater();
    }
    emit runAdded(run);
    emit runsChanged();
    run->start();
    return run;
}

QList<AutomationRun *> WorkflowEngine::activeRuns() const
{
    QList<AutomationRun *> list;
    for (AutomationRun *r : m_runs) {
        if (r->status() == AutomationRun::Running || r->status() == AutomationRun::Paused || r->status() == AutomationRun::Pending) {
            list << r;
        }
    }
    return list;
}

AutomationRun *WorkflowEngine::run(const QString &id) const
{
    for (AutomationRun *r : m_runs) {
        if (r->id() == id) {
            return r;
        }
    }
    return nullptr;
}

void WorkflowEngine::remove(AutomationRun *run)
{
    if (!run || !m_runs.removeOne(run)) {
        return;
    }
    emit runRemoved(run->id());
    run->deleteLater();
    emit runsChanged();
}

void WorkflowEngine::clearFinished()
{
    const QList<AutomationRun *> copy = m_runs;
    for (AutomationRun *r : copy) {
        if (r->status() == AutomationRun::Completed || r->status() == AutomationRun::Failed || r->status() == AutomationRun::Cancelled) {
            remove(r);
        }
    }
}

QStringList WorkflowEngine::resolveTargets(const QString &mode, const QString &group, const QStringList &selection)
{
    DeviceRegistry &registry = DeviceRegistry::instance();
    if (mode == QLatin1String("group")) {
        QStringList online;
        for (const QString &id : registry.membersOf(group)) {
            if (registry.get(id).isOnline()) {
                online << id;
            }
        }
        return online;
    }
    if (mode == QLatin1String("all") || mode == QLatin1String("online")) {
        return registry.sorted(DeviceRegistry::SortKey::Number, true, registry.onlineIds());
    }
    if (mode == QLatin1String("devices")) {
        return selection;
    }
    // selection: only online members
    QStringList online;
    for (const QString &id : selection) {
        if (registry.get(id).isOnline()) {
            online << id;
        }
    }
    return online;
}

Workflow WorkflowEngine::loadWorkflow(const QString &nameOrId, bool *found)
{
    const QList<WorkflowRow> rows = WorkflowRepository::loadAll();
    for (const WorkflowRow &r : rows) {
        if (r.id == nameOrId || r.name.compare(nameOrId, Qt::CaseInsensitive) == 0) {
            if (found) {
                *found = true;
            }
            return Workflow::fromJsonText(r.json);
        }
    }
    if (found) {
        *found = false;
    }
    return Workflow();
}

QStringList WorkflowEngine::workflowNames()
{
    QStringList names;
    for (const WorkflowRow &r : WorkflowRepository::loadAll()) {
        names << r.name;
    }
    return names;
}

} // namespace farm
