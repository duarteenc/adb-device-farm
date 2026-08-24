#ifndef FARM_ADB_ADBQUOTE_H
#define FARM_ADB_ADBQUOTE_H

#include <QString>
#include <QStringList>

namespace farm {
namespace adb {

/**
 * Quote a single argument for the Android `sh` on the device side of `adb shell`.
 * Wraps in single quotes and escapes embedded single quotes ('\'') so paths with
 * spaces, package names and arbitrary file names can never break out of the
 * intended command. Safe for any input, including empty strings.
 */
inline QString shellQuote(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("''");
    }
    bool safe = true;
    for (const QChar c : arg) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.')
              || c == QLatin1Char('/') || c == QLatin1Char(':') || c == QLatin1Char('@') || c == QLatin1Char('=')
              || c == QLatin1Char('+') || c == QLatin1Char(','))) {
            safe = false;
            break;
        }
    }
    if (safe) {
        return arg;
    }
    QString out = arg;
    out.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

/// Join already-separate arguments into one properly quoted device shell command line.
inline QString shellJoin(const QStringList &args)
{
    QStringList quoted;
    quoted.reserve(args.size());
    for (const QString &a : args) {
        quoted << shellQuote(a);
    }
    return quoted.join(QLatin1Char(' '));
}

/**
 * Escape text for `input text` (the framework's `input` tool treats spaces as
 * separators and interprets some characters). Non-ASCII must go through the
 * scrcpy control channel or the helper app instead.
 */
inline QString inputTextEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() * 2);
    for (const QChar c : text) {
        if (c == QLatin1Char(' ')) {
            out += QStringLiteral("%s");
        } else if (QStringLiteral("()<>|;&*~\"'`\\$#[]{}").contains(c)) {
            out += QLatin1Char('\\');
            out += c;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace adb
} // namespace farm

#endif // FARM_ADB_ADBQUOTE_H
