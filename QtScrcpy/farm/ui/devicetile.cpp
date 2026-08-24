#include "devicetile.h"

#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QStyle>

#include <algorithm>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "core/farmsettings.h"
#include "farmtheme.h"
#include "performance/perfmonitor.h"
#include "qyuvopenglwidget.h"

namespace farm {

DeviceTile::DeviceTile(const QString &id, QWidget *parent)
    : QWidget(parent)
    , m_id(id)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);
    m_clock.start();
    m_record.id = id;

    // The OpenGL widget is created lazily (ensureVideo) when the first frame
    // arrives: 100+ placeholder tiles must not each hold a GL context.
    m_overlay = new QWidget(this);
    m_overlay->setObjectName(QStringLiteral("tileOverlay"));
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_numLabel = new QLabel(m_overlay);
    m_numLabel->setObjectName(QStringLiteral("tileNum"));
    m_numLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel = new QLabel(m_overlay);
    m_nameLabel->setObjectName(QStringLiteral("tileName"));
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_ipLabel = new QLabel(m_overlay);
    m_ipLabel->setObjectName(QStringLiteral("tileIp"));
    m_ipLabel->setAlignment(Qt::AlignCenter);
    m_metaLabel = new QLabel(m_overlay);
    m_metaLabel->setObjectName(QStringLiteral("tileMeta"));
    m_metaLabel->setAlignment(Qt::AlignCenter);
    m_stateBadge = new QLabel(m_overlay);
    m_stateBadge->setAlignment(Qt::AlignCenter);
    m_connBadge = new QLabel(m_overlay);
    m_connBadge->setAlignment(Qt::AlignCenter);

    for (QLabel *l : { m_numLabel, m_nameLabel, m_ipLabel, m_metaLabel, m_stateBadge, m_connBadge }) {
        l->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    auto *og = new QGridLayout(m_overlay);
    og->setContentsMargins(5, 3, 5, 4);
    og->setHorizontalSpacing(4);
    og->setVerticalSpacing(0);
    og->addWidget(m_connBadge, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    og->addWidget(m_numLabel, 0, 1, Qt::AlignCenter);
    og->addWidget(m_stateBadge, 0, 2, Qt::AlignRight | Qt::AlignTop);
    og->addWidget(m_nameLabel, 1, 0, 1, 3);
    og->addWidget(m_ipLabel, 2, 0, 1, 3);
    og->addWidget(m_metaLabel, 3, 0, 1, 3);
    og->setColumnStretch(0, 1);
    og->setColumnStretch(2, 1);

    m_spinner = new QPropertyAnimation(this, "spinnerRotation", this);
    m_spinner->setDuration(1000);
    m_spinner->setStartValue(0);
    m_spinner->setEndValue(360);
    m_spinner->setLoopCount(-1);
    connect(m_spinner, &QPropertyAnimation::valueChanged, this, [this]() { update(); });

    connect(this, &DeviceTile::fpsUpdated, this, [this](quint32 fps) {
        m_fps = fps;
        refreshOverlay();
    }, Qt::QueuedConnection);

    setControllable(false);
    setTileWidth(m_tileWidth);
    applyBorder();
    refreshOverlay();
    placeChildren();
}

DeviceTile::~DeviceTile() = default;

QWidget *DeviceTile::videoWidget() const
{
    return m_video;
}

void DeviceTile::ensureVideo()
{
    if (m_video) {
        return;
    }
    m_video = new QYUVOpenGLWidget(this);
    m_video->setFocusPolicy(Qt::StrongFocus);
    m_video->installEventFilter(this);
    m_video->setAttribute(Qt::WA_TransparentForMouseEvents, !m_controllable);
    m_video->setOnPainted([this]() {
        if (m_frameArrivedNs > 0) {
            PerfMonitor::instance().countDisplayed((m_clock.nsecsElapsed() - m_frameArrivedNs) / 1000);
            m_frameArrivedNs = 0;
        }
    });
    placeChildren();
    m_video->show();
    m_overlay->raise();
}

void DeviceTile::releaseVideo()
{
    if (!m_video) {
        return;
    }
    m_video->removeEventFilter(this);
    m_video->hide();
    m_video->deleteLater();
    m_video = nullptr;
    m_hasFrame = false;
}

void DeviceTile::placeChildren()
{
    const QRect inner = rect().adjusted(2, 2, -2, -2);
    if (m_video) {
        m_video->setGeometry(inner);
    }
    m_overlay->setGeometry(inner.x(), inner.y(), inner.width(), std::min(inner.height(), m_overlay->sizeHint().height()));
    m_overlay->raise();
}

void DeviceTile::setRecord(const DeviceRecord &record)
{
    const bool spinning = record.state == DeviceState::Connecting || record.state == DeviceState::Reconnecting;
    m_record = record;
    if (spinning && m_spinner->state() != QAbstractAnimation::Running) {
        m_spinner->start();
    } else if (!spinning && m_spinner->state() == QAbstractAnimation::Running) {
        m_spinner->stop();
    }
    refreshOverlay();
    applyBorder();
    update();
}

void DeviceTile::setDensity(Density density)
{
    m_density = density;
    layoutOverlay();
    refreshOverlay();
}

void DeviceTile::layoutOverlay()
{
    const bool tiny = m_density == Density::Tiny;
    const bool compact = m_density == Density::Compact;
    m_nameLabel->setVisible(!tiny);
    m_ipLabel->setVisible(!tiny && !compact);
    m_metaLabel->setVisible(m_density == Density::Normal || m_density == Density::Large);
    m_connBadge->setVisible(!tiny);
    m_numLabel->setStyleSheet(tiny ? QStringLiteral("font-size:11px;") : m_density == Density::Large ? QStringLiteral("font-size:18px;") : QString());
}

void DeviceTile::setControllable(bool on)
{
    m_controllable = on;
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    if (m_video) {
        m_video->setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    }
    // Drops still need to reach the tile; the grid forwards them when transparent.
}

void DeviceTile::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    updateTextSelectionColor();
    applyBorder();
}

void DeviceTile::setSelectionPreview(bool preview)
{
    if (m_preview == preview) {
        return;
    }
    m_preview = preview;
    updateTextSelectionColor();
    applyBorder();
}

void DeviceTile::setUnderControl(bool on)
{
    m_underControl = on;
    applyBorder();
}

void DeviceTile::setHighlight(bool on)
{
    if (m_highlight != on) {
        m_highlight = on;
        applyBorder();
    }
}

void DeviceTile::setStreaming(bool streaming)
{
    if (m_streaming == streaming) {
        return;
    }
    m_streaming = streaming;
    if (!streaming) {
        releaseVideo();    // frees the GL context; the placeholder paints the state
        m_fps = 0;
    }
    refreshOverlay();
    update();
}

void DeviceTile::setRenderPriority(RenderPriority priority)
{
    m_priority = priority;
}

void DeviceTile::updateTextSelectionColor()
{
    const bool on = m_selected || m_preview;
    for (QLabel *l : { m_numLabel, m_nameLabel, m_ipLabel }) {
        if (l->property("sel").toBool() != on) {
            l->setProperty("sel", on);
            l->style()->unpolish(l);
            l->style()->polish(l);
        }
    }
}

void DeviceTile::applyBorder()
{
    QString color;
    int width = 1;
    QString background = theme::background().name();
    if (m_underControl) {
        color = theme::purple().name();
        width = m_density == Density::Tiny ? 3 : 6;
    } else if (m_preview) {
        color = QStringLiteral("#4ade80");
        width = m_density == Density::Tiny ? 3 : 5;
    } else if (m_selected) {
        color = theme::success().name();
        width = m_density == Density::Tiny ? 3 : 6;
    } else if (m_highlight) {
        color = theme::warning().name();
        width = 3;
    } else {
        color = theme::border().name();
        width = 1;
    }
    // Group colour stripe: a thin left border keeps it visible in every density.
    QString groupStripe;
    if (!m_record.group.isEmpty()) {
        groupStripe = QStringLiteral(" border-left:4px solid %1;").arg(QStringLiteral("__GROUP__"));
    }
    setStyleSheet(QStringLiteral("farm--DeviceTile{border:%1px solid %2; background:%3;%4}")
                      .arg(width)
                      .arg(color, background, groupStripe.replace(QLatin1String("__GROUP__"), m_record.props.value(QStringLiteral("_groupColor"), color).toString())));
}

void DeviceTile::setTileWidth(int width)
{
    m_tileWidth = width;
    setFixedWidth(width);
    setFixedHeight(static_cast<int>(width * m_frameAspect));
}

QSize DeviceTile::videoFrameSize() const
{
    return m_video ? m_video->frameSize() : QSize();
}

QSize DeviceTile::videoShowSize() const
{
    return m_video ? m_video->size() : rect().adjusted(2, 2, -2, -2).size();
}

void DeviceTile::refreshOverlay()
{
    const DeviceRecord &r = m_record;
    m_numLabel->setText(r.number > 0 ? QString::number(r.number) : QStringLiteral("·"));
    m_nameLabel->setText(r.friendlyName.isEmpty() ? (r.model.isEmpty() ? tr("device") : r.model) : r.friendlyName);
    m_ipLabel->setText(r.isTcp() ? r.host() : r.id);

    QStringList meta;
    if (r.battery >= 0) {
        meta << QStringLiteral("%1%2%").arg(r.charging ? QString(QChar(0x26A1)) : QString(), QString::number(r.battery));
    }
    if (r.temperatureC > 0 && m_density == Density::Large) {
        meta << QStringLiteral("%1°").arg(r.temperatureC, 0, 'f', 0);
    }
    if (m_streaming && m_fps > 0) {
        meta << QStringLiteral("%1fps").arg(m_fps);
    } else if (r.latencyMs >= 0) {
        meta << QStringLiteral("%1ms").arg(r.latencyMs);
    }
    if (r.wifiRssi < 0 && m_density == Density::Large) {
        meta << QStringLiteral("%1dBm").arg(r.wifiRssi);
    }
    if (r.automationRunning) {
        meta << QString(QChar(0x2699));
    }
    if (!r.keepAwakeStatus.isEmpty() && r.keepAwakeStatus.startsWith(QLatin1String("Failed"))) {
        meta << QString(QChar(0x2620));    // keep-awake failed marker
    }
    m_metaLabel->setText(meta.join(QStringLiteral("  ")));

    const QColor sc = theme::stateColor(static_cast<int>(r.state));
    m_stateBadge->setText(theme::stateGlyph(static_cast<int>(r.state)));
    m_stateBadge->setToolTip(deviceStateName(r.state) + (r.stateMessage.isEmpty() ? QString() : QStringLiteral(" — ") + r.stateMessage));
    m_stateBadge->setStyleSheet(QStringLiteral("color:%1; font-size:12px; font-weight:bold; background:rgba(0,0,0,0.35); border-radius:7px; padding:0 4px;").arg(sc.name()));

    const bool tcp = r.isTcp();
    m_connBadge->setText(tcp ? (r.connectionType == ConnectionType::Mdns ? QStringLiteral("mDNS") : QStringLiteral("WiFi")) : QStringLiteral("USB"));
    m_connBadge->setStyleSheet(tcp ? QStringLiteral("background:#2563eb; color:#ffffff; font-size:9px; font-weight:bold; border-radius:3px; padding:1px 4px;")
                                   : QStringLiteral("background:#f59e0b; color:#1a1206; font-size:9px; font-weight:bold; border-radius:3px; padding:1px 4px;"));
    setToolTip(QStringLiteral("%1 %2\n%3\n%4%5\n%6")
                   .arg(r.numberString(), r.displayName(), r.id, deviceStateName(r.state), r.stateMessage.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(r.stateMessage),
                        r.group.isEmpty() ? tr("no group") : tr("Group: %1").arg(r.group)));
}

void DeviceTile::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV)
{
    PerfMonitor &perf = PerfMonitor::instance();
    perf.countDecoded();
    if (m_priority == RenderPriority::Hidden) {
        perf.countDropped();
        m_stale = true;
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_priority == RenderPriority::Offscreen) {
        const int fps = FarmSettings::instance().offscreenFps();
        if (fps <= 0 || (now - m_lastUploadMs) < (1000 / fps)) {
            perf.countDropped();
            m_stale = true;
            return;
        }
    }
    const qint64 t0 = m_clock.nsecsElapsed();
    if (width > 0 && height > 0) {
        const double aspect = static_cast<double>(height) / static_cast<double>(width);
        if (qAbs(aspect - m_frameAspect) > 0.01) {
            m_frameAspect = aspect;
            setFixedHeight(static_cast<int>(m_tileWidth * m_frameAspect));
        }
    }
    m_streaming = true;
    ensureVideo();
    m_video->setFrameSize(QSize(width, height));
    m_frameArrivedNs = t0;
    m_video->updateTextures(dataY, dataU, dataV, static_cast<quint32>(linesizeY), static_cast<quint32>(linesizeU), static_cast<quint32>(linesizeV));
    m_lastUploadMs = now;
    m_stale = false;
    if (!m_hasFrame) {
        m_hasFrame = true;
        update();
    }
    perf.countRendered((m_clock.nsecsElapsed() - t0) / 1000);
}

void DeviceTile::updateFPS(quint32 fps)
{
    emit fpsUpdated(fps);
}

bool DeviceTile::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_video) {
        return QWidget::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::RightButton) {
            emit contextMenuRequested(m_id, me->globalPosition().toPoint());
            return true;
        }
        m_video->setFocus();
        emit clicked(m_id, me->modifiers());
        emit mouseInput(m_id, me);
        break;
    }
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        emit mouseInput(m_id, static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonDblClick:
        emit doubleClicked(m_id);
        break;
    case QEvent::Wheel:
        emit wheelInput(m_id, static_cast<QWheelEvent *>(event));
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        emit keyInput(m_id, static_cast<QKeyEvent *>(event));
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void DeviceTile::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(m_id, event->globalPos());
}

void DeviceTile::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DeviceTile::dropEvent(QDropEvent *event)
{
    QStringList files;
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &u : urls) {
        if (u.isLocalFile()) {
            files << u.toLocalFile();
        }
    }
    if (!files.isEmpty()) {
        emit filesDropped(m_id, files);
        event->acceptProposedAction();
    }
}

void DeviceTile::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeChildren();
}

void DeviceTile::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    const DeviceRecord &r = m_record;
    const bool showUnderlay = !m_video || !m_hasFrame;
    if (!showUnderlay) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect area = rect().adjusted(3, 3, -3, -3);
    // Placeholder body: darker card with a big number and the state.
    p.fillRect(area, QColor(0x0f, 0x14, 0x22));
    const QColor sc = theme::stateColor(static_cast<int>(r.state));
    if (r.state == DeviceState::Connecting || r.state == DeviceState::Reconnecting) {
        p.save();
        p.translate(area.center());
        p.rotate(m_spinnerRotation);
        QPen pen(theme::accent(), 2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const int s = m_density == Density::Tiny ? 8 : 16;
        const int g = 3;
        p.drawRect(-s - g, -s - g, s, s);
        p.drawRect(g, -s - g, s, s);
        p.drawRect(-s - g, g, s, s);
        p.drawRect(g, g, s, s);
        p.restore();
    } else {
        QFont f = p.font();
        f.setPointSize(m_density == Density::Tiny ? 8 : 12);
        f.setBold(true);
        p.setFont(f);
        p.setPen(sc);
        QRect textRect = area;
        textRect.setTop(area.center().y() - 10);
        p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, deviceStateName(r.state).toUpper());
        if (m_density != Density::Tiny && !r.stateMessage.isEmpty()) {
            f.setPointSize(8);
            f.setBold(false);
            p.setFont(f);
            p.setPen(theme::textMuted());
            QRect msgRect = area.adjusted(6, 0, -6, 0);
            msgRect.setTop(area.center().y() + 12);
            p.drawText(msgRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, r.stateMessage);
        }
    }
}

} // namespace farm
