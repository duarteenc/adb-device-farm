#include "ocrprovider.h"

#include <QMutex>

#include "../core/farmsettings.h"

#if defined(_MSC_VER) && __has_include(<winrt/Windows.Media.Ocr.h>)
#define FARM_HAVE_WINRT_OCR 1
#pragma warning(push, 0)
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma warning(pop)
#endif

namespace farm {

QRect OcrResult::find(const QString &needle) const
{
    const QString n = needle.trimmed();
    if (n.isEmpty()) {
        return QRect();
    }
    for (const OcrLine &line : lines) {
        // Exact word match first, then a run of consecutive words, then substring of the line.
        for (const OcrWord &w : line.words) {
            if (w.text.compare(n, Qt::CaseInsensitive) == 0) {
                return w.rect;
            }
        }
        const QStringList tokens = n.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.size() > 1) {
            for (int i = 0; i + tokens.size() <= line.words.size(); ++i) {
                bool all = true;
                QRect r;
                for (int k = 0; k < tokens.size(); ++k) {
                    if (line.words.at(i + k).text.compare(tokens.at(k), Qt::CaseInsensitive) != 0) {
                        all = false;
                        break;
                    }
                    r = r.isNull() ? line.words.at(i + k).rect : r.united(line.words.at(i + k).rect);
                }
                if (all) {
                    return r;
                }
            }
        }
        if (line.text.contains(n, Qt::CaseInsensitive)) {
            for (const OcrWord &w : line.words) {
                if (w.text.contains(n, Qt::CaseInsensitive) || n.contains(w.text, Qt::CaseInsensitive)) {
                    return w.rect;
                }
            }
            return line.rect;
        }
    }
    return QRect();
}

#ifdef FARM_HAVE_WINRT_OCR

namespace {
using namespace winrt;
using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage::Streams;

void ensureApartment()
{
    thread_local bool initialized = false;
    if (!initialized) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            // already initialised on this thread (e.g. Qt's STA GUI thread)
        }
        initialized = true;
    }
}

OcrEngine createEngine()
{
    OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
    if (!engine) {
        const auto langs = OcrEngine::AvailableRecognizerLanguages();
        if (langs.Size() > 0) {
            engine = OcrEngine::TryCreateFromLanguage(langs.GetAt(0));
        }
    }
    return engine;
}
} // namespace

bool OcrProvider::available()
{
    if (FarmSettings::instance().stringValue(QStringLiteral("automation/ocr"), QStringLiteral("auto")) == QLatin1String("none")) {
        return false;
    }
    try {
        ensureApartment();
        return OcrEngine::AvailableRecognizerLanguages().Size() > 0;
    } catch (...) {
        return false;
    }
}

QString OcrProvider::backendName()
{
    return QStringLiteral("windows-ocr");
}

QString OcrProvider::availableLanguages()
{
    try {
        ensureApartment();
        QStringList list;
        for (const auto &l : OcrEngine::AvailableRecognizerLanguages()) {
            list << QString::fromStdWString(std::wstring(l.LanguageTag()));
        }
        return list.join(QStringLiteral(", "));
    } catch (...) {
        return QString();
    }
}

OcrResult OcrProvider::recognize(const QImage &imageIn)
{
    OcrResult result;
    if (imageIn.isNull()) {
        result.error = QStringLiteral("empty image");
        return result;
    }
    try {
        ensureApartment();
        OcrEngine engine = createEngine();
        if (!engine) {
            result.error = QStringLiteral("no OCR language pack installed (Settings > Time & Language > Language > add a language with 'Basic typing')");
            return result;
        }
        // The engine wants images below its MaxImageDimension; scale down if needed.
        QImage image = imageIn.convertToFormat(QImage::Format_ARGB32);
        const uint32_t maxDim = OcrEngine::MaxImageDimension();
        if (static_cast<uint32_t>(std::max(image.width(), image.height())) > maxDim) {
            image = image.scaled(static_cast<int>(maxDim), static_cast<int>(maxDim), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        const double sx = static_cast<double>(imageIn.width()) / image.width();
        const double sy = static_cast<double>(imageIn.height()) / image.height();
        const uint32_t size = static_cast<uint32_t>(image.sizeInBytes());
        Buffer buffer(size);
        memcpy(buffer.data(), image.constBits(), size);
        buffer.Length(size);
        SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Bgra8, image.width(), image.height(), BitmapAlphaMode::Ignore);
        {
            winrt::Windows::Media::Ocr::OcrResult ocr = engine.RecognizeAsync(bitmap).get();
            result.text = QString::fromStdWString(std::wstring(ocr.Text()));
            for (const auto &line : ocr.Lines()) {
                OcrLine l;
                l.text = QString::fromStdWString(std::wstring(line.Text()));
                for (const auto &word : line.Words()) {
                    OcrWord w;
                    w.text = QString::fromStdWString(std::wstring(word.Text()));
                    const auto r = word.BoundingRect();
                    w.rect = QRect(static_cast<int>(r.X * sx), static_cast<int>(r.Y * sy), static_cast<int>(r.Width * sx), static_cast<int>(r.Height * sy));
                    l.rect = l.rect.isNull() ? w.rect : l.rect.united(w.rect);
                    l.words.append(w);
                }
                result.lines.append(l);
            }
        }
    } catch (const winrt::hresult_error &e) {
        result.error = QStringLiteral("Windows OCR error 0x%1: %2").arg(static_cast<quint32>(e.code()), 8, 16, QLatin1Char('0')).arg(QString::fromStdWString(std::wstring(e.message())));
    } catch (...) {
        result.error = QStringLiteral("Windows OCR failed");
    }
    return result;
}

#else

bool OcrProvider::available()
{
    return false;
}

QString OcrProvider::backendName()
{
    return QStringLiteral("none");
}

QString OcrProvider::availableLanguages()
{
    return QString();
}

OcrResult OcrProvider::recognize(const QImage &)
{
    OcrResult r;
    r.error = QStringLiteral("no OCR backend compiled in (needs the Windows SDK C++/WinRT headers or Tesseract)");
    return r;
}

#endif

} // namespace farm
