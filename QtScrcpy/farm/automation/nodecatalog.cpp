#include "nodecatalog.h"

namespace farm {

namespace {
ParamSpec P(const char *key, const char *label, const char *type, const QVariant &def = QVariant(), bool required = false, const char *help = "", const QStringList &options = QStringList())
{
    ParamSpec p;
    p.key = QLatin1String(key);
    p.label = QLatin1String(label);
    p.type = QLatin1String(type);
    p.defaultValue = def;
    p.required = required;
    p.help = QLatin1String(help);
    p.options = options;
    return p;
}

NodeSpec N(const char *type, const char *title, const char *category, const QStringList &outputs, const QList<ParamSpec> &params, const char *help = "", bool risky = false, int timeoutMs = 30000)
{
    NodeSpec n;
    n.type = QLatin1String(type);
    n.title = QLatin1String(title);
    n.category = QLatin1String(category);
    n.outputs = outputs;
    n.params = params;
    n.help = QLatin1String(help);
    n.risky = risky;
    n.defaultTimeoutMs = timeoutMs;
    return n;
}

const QStringList OUT{ QStringLiteral("out") };
const QStringList BOOL{ QStringLiteral("true"), QStringLiteral("false") };
const QStringList LOOP{ QStringLiteral("body"), QStringLiteral("done") };
const QStringList FOUND{ QStringLiteral("found"), QStringLiteral("timeout") };

QList<NodeSpec> build()
{
    QList<NodeSpec> list;
    // ---- Flow / Device ----
    list << N("flow.start", "Start", "Flow", OUT,
              { P("targetsMode", "Default targets", "enum", "selection", false, "Used when run from the scheduler or CLI", { "selection", "group", "all", "online" }),
                P("group", "Group", "string", ""), P("concurrency", "Parallel devices", "int", 5, false, "How many devices run at the same time"),
                P("sequential", "Sequential (one device at a time)", "bool", false) },
              "Entry point. Every target device runs the workflow from here; use Concurrency for 'Parallel Devices' and Sequential for 'For Each Device'.");
    list << N("flow.end", "End", "Flow", {}, {}, "Finishes the device run successfully.");
    list << N("flow.fail", "Fail", "Flow", {}, { P("message", "Message", "string", "failed", true) }, "Ends the device run as failed (captures an error screenshot).");
    list << N("flow.runWorkflow", "Run workflow", "Flow", OUT, { P("workflow", "Workflow name or id", "string", "", true) }, "Runs another saved workflow inline (max depth 5).");
    list << N("device.property", "Device property → variable", "Device", OUT,
              { P("property", "Property", "enum", "number", true, "", { "id", "number", "name", "model", "ip", "group", "battery", "androidVersion", "serial", "screenWidth", "screenHeight" }),
                P("variable", "Variable", "variable", "device", true) });
    list << N("device.wake", "Wake device", "Device", OUT, {}, "KEYCODE_WAKEUP; reports 'locked' in the log if a keyguard is showing.");
    list << N("device.keepAwake", "Apply keep-awake policy", "Device", OUT, {}, "");
    // ---- Interaction ----
    const QList<ParamSpec> xy{ P("x", "X", "double", 0.5, true, "0-1 = fraction of the screen, >1 = pixels"), P("y", "Y", "double", 0.5, true) };
    list << N("input.tap", "Tap", "Interaction", OUT, xy);
    list << N("input.doubleTap", "Double tap", "Interaction", OUT, xy);
    list << N("input.longPress", "Long press", "Interaction", OUT, xy + QList<ParamSpec>{ P("durationMs", "Duration (ms)", "int", 800) });
    list << N("input.swipe", "Swipe", "Interaction", OUT,
              { P("x1", "From X", "double", 0.5, true), P("y1", "From Y", "double", 0.8, true), P("x2", "To X", "double", 0.5, true), P("y2", "To Y", "double", 0.2, true), P("durationMs", "Duration (ms)", "int", 300) });
    list << N("input.scroll", "Scroll", "Interaction", OUT,
              { P("direction", "Direction", "enum", "down", true, "", { "down", "up", "left", "right" }), P("amount", "Amount (0-1 of screen)", "double", 0.5), P("durationMs", "Duration (ms)", "int", 300) });
    list << N("input.text", "Input text", "Interaction", OUT, { P("text", "Text", "text", "", true, "Supports ${variables}"), P("clearFirst", "Select all + delete first", "bool", false) },
              "Uses the mirror channel (Unicode) when the device is mirroring, otherwise `input text` (ASCII).");
    list << N("input.key", "Press key", "Interaction", OUT, { P("key", "Key", "keycode", "KEYCODE_ENTER", true, "Android keycode name or number") });
    list << N("input.back", "Back", "Interaction", OUT, {});
    list << N("input.home", "Home", "Interaction", OUT, {});
    list << N("input.recent", "Recent apps", "Interaction", OUT, {});
    // ---- Application ----
    list << N("app.launch", "Launch app", "Application", OUT, { P("package", "Package", "package", "", true) });
    list << N("app.forceStop", "Force stop app", "Application", OUT, { P("package", "Package", "package", "", true) });
    list << N("app.clearData", "Clear app data", "Application", OUT, { P("package", "Package", "package", "", true) }, "", true);
    list << N("app.install", "Install APK", "Application", OUT, { P("apk", "APK file", "file", "", true), P("reinstall", "Reinstall (-r)", "bool", true), P("grant", "Grant all permissions (-g)", "bool", false) }, "", false, 300000);
    list << N("app.uninstall", "Uninstall", "Application", OUT, { P("package", "Package", "package", "", true) }, "", true, 60000);
    list << N("app.waitFor", "Wait for app in foreground", "Application", FOUND, { P("package", "Package", "package", "", true), P("timeoutMs", "Timeout (ms)", "int", 15000), P("intervalMs", "Poll interval (ms)", "int", 1000) }, "", false, 120000);
    list << N("app.isForeground", "App in foreground?", "Application", BOOL, { P("package", "Package", "package", "", true) });
    // ---- Timing ----
    list << N("time.wait", "Wait", "Timing", OUT, { P("ms", "Milliseconds", "duration", 1000, true) });
    list << N("time.waitRandom", "Wait random", "Timing", OUT, { P("minMs", "Min (ms)", "duration", 500, true), P("maxMs", "Max (ms)", "duration", 1500, true) }, "Randomised pause for timing resilience.");
    // ---- Logic ----
    list << N("logic.if", "If", "Logic", BOOL, { P("condition", "Condition", "expression", "${found} == true", true, "e.g. ${battery} < 20, ${text} contains \"OK\"") });
    list << N("logic.loop", "Repeat N times", "Logic", LOOP, { P("count", "Count", "int", 3, true), P("indexVariable", "Index variable", "variable", "i") });
    list << N("logic.while", "While", "Logic", LOOP, { P("condition", "Condition", "expression", "${i} < 5", true), P("maxIterations", "Max iterations", "int", 1000) });
    list << N("logic.break", "Break", "Logic", {}, {}, "Leaves the innermost loop.");
    list << N("logic.continue", "Continue", "Logic", {}, {}, "Next iteration of the innermost loop.");
    list << N("logic.switch", "Switch", "Logic", { QStringLiteral("default") }, { P("value", "Value", "expression", "${group}", true), P("cases", "Cases (comma separated)", "string", "A,B") },
              "Outputs are case:<value> for each case plus default.");
    // ---- Variables ----
    list << N("var.set", "Set variable", "Variables", OUT, { P("name", "Name", "variable", "value", true), P("value", "Value", "expression", "", false, "Text, number, true/false, or ${expression}") });
    list << N("var.increment", "Increment", "Variables", OUT, { P("name", "Name", "variable", "counter", true), P("by", "By", "double", 1) });
    list << N("var.listAppend", "Append to list", "Variables", OUT, { P("name", "List variable", "variable", "items", true), P("value", "Value", "expression", "") });
    // ---- ADB ----
    list << N("adb.shell", "Shell command", "ADB", OUT, { P("script", "Command", "text", "getprop ro.product.model", true), P("saveTo", "Save output to", "variable", "output"), P("failOnError", "Fail on non-zero exit", "bool", true) });
    list << N("adb.exec", "adb command", "ADB", OUT, { P("args", "Arguments (without adb / -s)", "string", "reboot", true), P("saveTo", "Save output to", "variable", "output") }, "", true, 120000);
    list << N("adb.getprop", "Read property", "ADB", OUT, { P("property", "Property", "string", "ro.build.version.release", true), P("saveTo", "Save to", "variable", "prop", true) });
    // ---- Screen ----
    list << N("screen.screenshot", "Screenshot", "Screen", OUT, { P("tag", "File tag", "string", "shot") }, "Saved under the run folder as <device>/<tag>-<step>.png");
    list << N("screen.findImage", "Find image", "Screen", FOUND, { P("image", "Template image", "image", "", true), P("threshold", "Match threshold (0-1)", "double", 0.85), P("region", "Region x,y,w,h (fractions, optional)", "string", ""), P("saveTo", "Save match to", "variable", "match") },
              "Template matching (normalised cross-correlation). The match centre is available as ${match.x}/${match.y} (fractions).");
    list << N("screen.waitForImage", "Wait for image", "Screen", FOUND, { P("image", "Template image", "image", "", true), P("threshold", "Threshold", "double", 0.85), P("timeoutMs", "Timeout (ms)", "int", 15000), P("intervalMs", "Interval (ms)", "int", 1000), P("untilGone", "Wait until gone", "bool", false), P("saveTo", "Save match to", "variable", "match") }, "", false, 180000);
    list << N("screen.tapImage", "Tap image", "Screen", FOUND, { P("image", "Template image", "image", "", true), P("threshold", "Threshold", "double", 0.85) });
    list << N("screen.ocr", "OCR", "Screen", OUT, { P("region", "Region x,y,w,h (fractions, optional)", "string", ""), P("saveTo", "Save text to", "variable", "text", true) }, "Local OCR (Windows built-in engine); no cloud service.");
    list << N("screen.waitForText", "Wait for text", "Screen", FOUND, { P("text", "Text", "string", "", true), P("timeoutMs", "Timeout (ms)", "int", 15000), P("intervalMs", "Interval (ms)", "int", 1500), P("saveTo", "Save match to", "variable", "match") }, "", false, 180000);
    list << N("screen.tapText", "Tap text", "Screen", FOUND, { P("text", "Text", "string", "", true) }, "Finds the text with OCR and taps its centre.");
    list << N("ui.waitForElement", "Wait for UI element", "Screen", FOUND, { P("selector", "Selector", "selector", "text=OK", true, "text=, textContains=, resourceId=, contentDesc=, class="), P("timeoutMs", "Timeout (ms)", "int", 15000), P("intervalMs", "Interval (ms)", "int", 1000), P("saveTo", "Save bounds to", "variable", "element") }, "Uses the Android UI hierarchy (uiautomator dump) — robust against layout changes.", false, 180000);
    list << N("ui.tapElement", "Tap UI element", "Screen", FOUND, { P("selector", "Selector", "selector", "text=OK", true) });
    list << N("ui.exists", "UI element exists?", "Screen", BOOL, { P("selector", "Selector", "selector", "resourceId=android:id/button1", true) });
    list << N("ui.dump", "Dump UI hierarchy → variable", "Screen", OUT, { P("saveTo", "Save XML to", "variable", "hierarchy", true) });
    // ---- Files ----
    list << N("file.push", "Push file", "Files", OUT, { P("local", "Local file", "file", "", true), P("remote", "Remote directory", "string", "/sdcard/Download", true) }, "", false, 600000);
    list << N("file.pull", "Pull file", "Files", OUT, { P("remote", "Remote path", "string", "", true), P("localDir", "Local directory (blank = run folder)", "string", "") }, "", false, 600000);
    list << N("file.exists", "Remote file exists?", "Files", BOOL, { P("remote", "Remote path", "string", "", true) });
    list << N("file.delete", "Delete remote file", "Files", OUT, { P("remote", "Remote path", "string", "", true) }, "", true);
    // ---- Logging ----
    list << N("log.message", "Log message", "Logging", OUT, { P("level", "Level", "enum", "info", false, "", { "info", "warning", "error" }), P("message", "Message", "text", "", true) });
    list << N("log.screenshot", "Save screenshot to log", "Logging", OUT, { P("tag", "Tag", "string", "log") });
    list << N("log.errorReport", "Create error report", "Logging", OUT, { P("message", "Message", "text", "checkpoint") }, "Screenshot + UI dump + variables saved in the run folder.");
    return list;
}
} // namespace

const QList<NodeSpec> &NodeCatalog::all()
{
    static const QList<NodeSpec> list = build();
    return list;
}

bool NodeCatalog::has(const QString &type)
{
    for (const NodeSpec &n : all()) {
        if (n.type == type) {
            return true;
        }
    }
    return false;
}

NodeSpec NodeCatalog::spec(const QString &type)
{
    for (const NodeSpec &n : all()) {
        if (n.type == type) {
            return n;
        }
    }
    NodeSpec unknown;
    unknown.type = type;
    unknown.title = type;
    unknown.category = QStringLiteral("Unknown");
    return unknown;
}

QStringList NodeCatalog::categories()
{
    QStringList cats;
    for (const NodeSpec &n : all()) {
        if (!cats.contains(n.category)) {
            cats << n.category;
        }
    }
    return cats;
}

QList<NodeSpec> NodeCatalog::inCategory(const QString &category)
{
    QList<NodeSpec> list;
    for (const NodeSpec &n : all()) {
        if (n.category == category) {
            list << n;
        }
    }
    return list;
}

QVariantMap NodeCatalog::defaultParams(const QString &type)
{
    QVariantMap m;
    for (const ParamSpec &p : spec(type).params) {
        m.insert(p.key, p.defaultValue);
    }
    return m;
}

QString NodeCatalog::describeForPrompt()
{
    QString s;
    for (const NodeSpec &n : all()) {
        QStringList params;
        for (const ParamSpec &p : n.params) {
            params << QStringLiteral("%1:%2%3").arg(p.key, p.type, p.options.isEmpty() ? QString() : QStringLiteral("(%1)").arg(p.options.join(QLatin1Char('|'))));
        }
        s += QStringLiteral("- %1 [%2] outputs=%3 params={%4}%5\n").arg(n.type, n.title, n.outputs.isEmpty() ? QStringLiteral("none") : n.outputs.join(QLatin1Char(',')), params.join(QStringLiteral(", ")), n.risky ? QStringLiteral(" RISKY") : QString());
    }
    return s;
}

} // namespace farm
