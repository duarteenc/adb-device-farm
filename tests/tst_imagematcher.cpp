#include <QPainter>
#include <QtTest>

#include "automation/imagematcher.h"

using namespace farm;

class TestImageMatcher : public QObject
{
    Q_OBJECT
    static QImage screen()
    {
        // A 540x960 synthetic "screen": gradient background, a distinctive button at (300,600)
        // and a second identical button at (60,120).
        QImage img(540, 960, QImage::Format_RGB32);
        QPainter p(&img);
        QLinearGradient g(0, 0, 0, 960);
        g.setColorAt(0, QColor(40, 60, 90));
        g.setColorAt(1, QColor(200, 210, 230));
        p.fillRect(img.rect(), g);
        for (const QPoint &at : { QPoint(300, 600), QPoint(60, 120) }) {
            // A "button": green rounded rect with a white pill and a dark diagonal stripe
            // (no text — QFontDatabase would require a QGuiApplication in this headless test).
            p.setBrush(QColor(30, 140, 60));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRect(at, QSize(120, 44)), 6, 6);
            p.setBrush(Qt::white);
            p.drawRoundedRect(QRect(at + QPoint(20, 14), QSize(80, 16)), 8, 8);
            p.setPen(QPen(QColor(20, 40, 30), 3));
            p.drawLine(at + QPoint(30, 8), at + QPoint(90, 36));
            p.setPen(Qt::NoPen);
        }
        p.setBrush(QColor(200, 40, 40));
        p.drawRect(QRect(400, 100, 100, 44));
        return img;
    }

private slots:
    void findsTemplate()
    {
        const QImage hay = screen();
        const QImage needle = hay.copy(QRect(300, 600, 120, 44));
        QElapsedTimer t;
        t.start();
        const QList<ImageMatch> m = ImageMatcher::find(hay, needle, 0.85, QRect(), 5);
        const qint64 ms = t.elapsed();
        QVERIFY(!m.isEmpty());
        QVERIFY(m.first().score > 0.95);
        QVERIFY(qAbs(m.first().rect.x() - 300) <= 2);
        QVERIFY(qAbs(m.first().rect.y() - 600) <= 2);
        QCOMPARE(m.size(), 2);    // both LOGIN buttons
        QVERIFY(qAbs(m.at(1).rect.x() - 60) <= 2);
        QVERIFY2(ms < 3000, qPrintable(QStringLiteral("matching took %1 ms").arg(ms)));
    }

    void regionAndThreshold()
    {
        const QImage hay = screen();
        const QImage needle = hay.copy(QRect(300, 600, 120, 44));
        // restrict to the lower half: only one match
        QCOMPARE(ImageMatcher::find(hay, needle, 0.85, QRect(0, 480, 540, 480), 5).size(), 1);
        // a template that is not on screen
        QImage none(120, 44, QImage::Format_RGB32);
        none.fill(QColor(255, 0, 255));
        QVERIFY(ImageMatcher::find(hay, none, 0.9).isEmpty());
        // flat (zero-variance) template never matches
        QImage flat(20, 20, QImage::Format_RGB32);
        flat.fill(Qt::gray);
        QVERIFY(ImageMatcher::find(hay, flat, 0.5).isEmpty());
        QVERIFY(ImageMatcher::scoreAt(hay, needle, 300, 600) > 0.99);
        QVERIFY(ImageMatcher::scoreAt(hay, needle, 0, 0) < 0.6);
    }
};

QTEST_GUILESS_MAIN(TestImageMatcher)
#include "tst_imagematcher.moc"
