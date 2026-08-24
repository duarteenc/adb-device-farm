#include "imagematcher.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace farm {

namespace {
struct Gray
{
    int w = 0;
    int h = 0;
    std::vector<float> px;
    float at(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
};

Gray toGray(const QImage &imageIn, double scale)
{
    QImage image = imageIn;
    if (scale < 0.999) {
        image = image.scaled(std::max(1, static_cast<int>(image.width() * scale)), std::max(1, static_cast<int>(image.height() * scale)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_Grayscale8);
    Gray g;
    g.w = image.width();
    g.h = image.height();
    g.px.resize(static_cast<size_t>(g.w) * g.h);
    for (int y = 0; y < g.h; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < g.w; ++x) {
            g.px[static_cast<size_t>(y) * g.w + x] = line[x];
        }
    }
    return g;
}

struct Template
{
    Gray g;
    double mean = 0;
    double norm = 0;    // sqrt(sum((t-mean)^2))
    std::vector<float> centered;
};

Template prepare(const Gray &g)
{
    Template t;
    t.g = g;
    const size_t n = g.px.size();
    double sum = 0;
    for (float v : g.px) {
        sum += v;
    }
    t.mean = n ? sum / n : 0;
    t.centered.resize(n);
    double sq = 0;
    for (size_t i = 0; i < n; ++i) {
        const float c = g.px[i] - static_cast<float>(t.mean);
        t.centered[i] = c;
        sq += static_cast<double>(c) * c;
    }
    t.norm = std::sqrt(sq);
    return t;
}

// Integral images of haystack for fast window mean / variance.
struct Integral
{
    int w = 0;
    int h = 0;
    std::vector<double> sum;
    std::vector<double> sq;
    double rect(const std::vector<double> &tbl, int x, int y, int rw, int rh) const
    {
        const int W = w + 1;
        return tbl[static_cast<size_t>(y + rh) * W + (x + rw)] - tbl[static_cast<size_t>(y) * W + (x + rw)] - tbl[static_cast<size_t>(y + rh) * W + x] + tbl[static_cast<size_t>(y) * W + x];
    }
};

Integral integrate(const Gray &g)
{
    Integral I;
    I.w = g.w;
    I.h = g.h;
    const int W = g.w + 1;
    I.sum.assign(static_cast<size_t>(W) * (g.h + 1), 0.0);
    I.sq.assign(static_cast<size_t>(W) * (g.h + 1), 0.0);
    for (int y = 1; y <= g.h; ++y) {
        double rowSum = 0;
        double rowSq = 0;
        for (int x = 1; x <= g.w; ++x) {
            const double v = g.at(x - 1, y - 1);
            rowSum += v;
            rowSq += v * v;
            I.sum[static_cast<size_t>(y) * W + x] = I.sum[static_cast<size_t>(y - 1) * W + x] + rowSum;
            I.sq[static_cast<size_t>(y) * W + x] = I.sq[static_cast<size_t>(y - 1) * W + x] + rowSq;
        }
    }
    return I;
}

double ncc(const Gray &hay, const Integral &I, const Template &t, int x, int y)
{
    const int tw = t.g.w;
    const int th = t.g.h;
    if (x < 0 || y < 0 || x + tw > hay.w || y + th > hay.h || t.norm <= 1e-6) {
        return 0;
    }
    const double n = static_cast<double>(tw) * th;
    const double wsum = I.rect(I.sum, x, y, tw, th);
    const double wsq = I.rect(I.sq, x, y, tw, th);
    const double wmean = wsum / n;
    const double wvar = wsq - n * wmean * wmean;
    if (wvar <= 1e-6) {
        return 0;
    }
    double dot = 0;
    for (int j = 0; j < th; ++j) {
        const float *hrow = &hay.px[static_cast<size_t>(y + j) * hay.w + x];
        const float *trow = &t.centered[static_cast<size_t>(j) * tw];
        for (int i = 0; i < tw; ++i) {
            dot += static_cast<double>(hrow[i]) * trow[i];
        }
    }
    // sum((h - hmean)(t - tmean)) = sum(h * tc) - hmean * sum(tc) ; sum(tc) == 0
    return dot / (std::sqrt(wvar) * t.norm);
}
} // namespace

QString ImageMatcher::backendName()
{
#ifdef FARM_HAVE_OPENCV
    return QStringLiteral("opencv");
#else
    return QStringLiteral("native-ncc");
#endif
}

double ImageMatcher::scoreAt(const QImage &haystack, const QImage &needle, int x, int y)
{
    const Gray hay = toGray(haystack, 1.0);
    const Template t = prepare(toGray(needle, 1.0));
    const Integral I = integrate(hay);
    return ncc(hay, I, t, x, y);
}

QList<ImageMatch> ImageMatcher::find(const QImage &haystackIn, const QImage &needleIn, double threshold, const QRect &regionIn, int maxMatches)
{
    QList<ImageMatch> result;
    if (haystackIn.isNull() || needleIn.isNull() || needleIn.width() > haystackIn.width() || needleIn.height() > haystackIn.height()) {
        return result;
    }
    QRect region = regionIn.isValid() ? regionIn.intersected(haystackIn.rect()) : haystackIn.rect();
    if (region.width() < needleIn.width() || region.height() < needleIn.height()) {
        return result;
    }
    const QImage haystack = region == haystackIn.rect() ? haystackIn : haystackIn.copy(region);

    // Coarse level: keep the needle at least ~12 px wide so it stays distinctive.
    double scale = 1.0;
    const int target = 360;
    if (haystack.width() > target) {
        scale = static_cast<double>(target) / haystack.width();
        if (needleIn.width() * scale < 12 || needleIn.height() * scale < 8) {
            scale = std::max(12.0 / needleIn.width(), 8.0 / needleIn.height());
            scale = std::min(1.0, scale);
        }
    }
    const Gray hayC = toGray(haystack, scale);
    const Template tC = prepare(toGray(needleIn, scale));
    const Integral IC = integrate(hayC);
    struct Cand
    {
        int x;
        int y;
        double s;
    };
    std::vector<Cand> cands;
    const int stepX = 1;
    const int stepY = 1;
    const double coarseThreshold = std::max(0.3, threshold - 0.25);
    for (int y = 0; y + tC.g.h <= hayC.h; y += stepY) {
        for (int x = 0; x + tC.g.w <= hayC.w; x += stepX) {
            const double s = ncc(hayC, IC, tC, x, y);
            if (s >= coarseThreshold) {
                cands.push_back({ x, y, s });
            }
        }
    }
    if (cands.empty()) {
        return result;
    }
    std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) { return a.s > b.s; });
    if (cands.size() > 64) {
        cands.resize(64);
    }

    // Refine at full resolution around each coarse candidate.
    const Gray hayF = scale < 0.999 ? toGray(haystack, 1.0) : hayC;
    const Template tF = scale < 0.999 ? prepare(toGray(needleIn, 1.0)) : tC;
    const Integral IF = scale < 0.999 ? integrate(hayF) : IC;
    const int radius = scale < 0.999 ? static_cast<int>(std::ceil(1.0 / scale)) + 1 : 0;
    std::vector<Cand> refined;
    for (const Cand &c : cands) {
        const int cx = static_cast<int>(std::lround(c.x / scale));
        const int cy = static_cast<int>(std::lround(c.y / scale));
        Cand best{ cx, cy, -1 };
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                const double s = ncc(hayF, IF, tF, cx + dx, cy + dy);
                if (s > best.s) {
                    best = { cx + dx, cy + dy, s };
                }
            }
        }
        if (best.s >= threshold) {
            refined.push_back(best);
        }
    }
    std::sort(refined.begin(), refined.end(), [](const Cand &a, const Cand &b) { return a.s > b.s; });
    // Non-maximum suppression: drop candidates overlapping a better one.
    for (const Cand &c : refined) {
        const QRect r(region.x() + c.x, region.y() + c.y, needleIn.width(), needleIn.height());
        bool overlaps = false;
        for (const ImageMatch &m : result) {
            const QRect inter = m.rect.intersected(r);
            if (inter.width() * inter.height() > r.width() * r.height() / 2) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) {
            continue;
        }
        ImageMatch m;
        m.rect = r;
        m.score = c.s;
        result.append(m);
        if (result.size() >= std::max(1, maxMatches)) {
            break;
        }
    }
    return result;
}

} // namespace farm
