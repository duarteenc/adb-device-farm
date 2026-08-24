#ifndef FARM_AUTOMATION_OCRPROVIDER_H
#define FARM_AUTOMATION_OCRPROVIDER_H

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

namespace farm {

struct OcrWord
{
    QString text;
    QRect rect;
};

struct OcrLine
{
    QString text;
    QRect rect;
    QList<OcrWord> words;
};

struct OcrResult
{
    QString text;
    QList<OcrLine> lines;
    QString error;
    bool ok() const { return error.isEmpty(); }
    /// Bounding box of the first occurrence of `needle` (case-insensitive, may span words).
    QRect find(const QString &needle) const;
};

/**
 * Local OCR. Backend: the Windows built-in OCR engine (Windows.Media.Ocr, part
 * of Windows 10/11, no download, no cloud). Optional Tesseract backend when the
 * project is configured with FARM_HAVE_TESSERACT. Always runs off the GUI thread.
 */
class OcrProvider
{
public:
    static bool available();
    static QString backendName();
    static QString availableLanguages();
    static OcrResult recognize(const QImage &image);
};

} // namespace farm

#endif // FARM_AUTOMATION_OCRPROVIDER_H
