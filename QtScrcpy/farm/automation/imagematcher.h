#ifndef FARM_AUTOMATION_IMAGEMATCHER_H
#define FARM_AUTOMATION_IMAGEMATCHER_H

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QString>

namespace farm {

struct ImageMatch
{
    QRect rect;
    double score = 0;
    QPointF center() const { return QPointF(rect.x() + rect.width() / 2.0, rect.y() + rect.height() / 2.0); }
};

/**
 * Template matching without external dependencies: normalised cross-correlation
 * on grayscale, coarse search on a downscaled pyramid level followed by a
 * full-resolution refinement around the best candidates. Runs on the media/
 * automation lanes, never on the GUI thread. When the project is configured
 * with OpenCV (FARM_HAVE_OPENCV) the same API uses cv::matchTemplate.
 */
class ImageMatcher
{
public:
    static QString backendName();
    /// Find up to maxMatches occurrences of needle in haystack (score >= threshold), best first.
    static QList<ImageMatch> find(const QImage &haystack, const QImage &needle, double threshold = 0.85, const QRect &region = QRect(), int maxMatches = 1);
    /// Score at one position (0..1), for tests.
    static double scoreAt(const QImage &haystack, const QImage &needle, int x, int y);
};

} // namespace farm

#endif // FARM_AUTOMATION_IMAGEMATCHER_H
