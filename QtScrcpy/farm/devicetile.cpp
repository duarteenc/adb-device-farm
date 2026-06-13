#include "devicetile.h"

#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "qyuvopenglwidget.h"


DeviceTile::DeviceTile(const QString &serial, QWidget *parent)
    : QWidget(parent)
    , m_serial(serial)
    , m_statusText(tr("connecting…"))
{
    setAttribute(Qt::WA_StyledBackground, true);
    m_ip = serial.contains(':') ? serial.left(serial.indexOf(':')) : serial;

    // --- Video ---
    m_video = new QYUVOpenGLWidget(this);
    m_video->setFocusPolicy(Qt::StrongFocus);
    m_video->installEventFilter(this);

    // --- Overlay (number / model / ip) drawn on top of the screen ---
    m_overlay = new QWidget(m_video);
    m_overlay->setObjectName("tileOverlay");
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_numLabel = new QLabel(m_overlay);
    m_numLabel->setObjectName("tileNum");
    m_numLabel->setAlignment(Qt::AlignCenter);
    m_modelLabel = new QLabel(m_overlay);
    m_modelLabel->setObjectName("tileModel");
    m_modelLabel->setAlignment(Qt::AlignCenter);
    m_ipLabel = new QLabel(m_overlay);
    m_ipLabel->setObjectName("tileIp");
    m_ipLabel->setAlignment(Qt::AlignCenter);
    m_fpsLabel = new QLabel(m_overlay);
    m_fpsLabel->setObjectName("tileFps");
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    for (QLabel *l : {m_numLabel, m_modelLabel, m_ipLabel, m_fpsLabel}) {
        l->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    auto *og = new QGridLayout(m_overlay);
    og->setContentsMargins(6, 3, 6, 5);
    og->setHorizontalSpacing(4);
    og->setVerticalSpacing(0);
    og->addWidget(m_numLabel, 0, 1, Qt::AlignCenter);
    og->addWidget(m_fpsLabel, 0, 2);
    og->addWidget(m_modelLabel, 1, 0, 1, 3);
    og->addWidget(m_ipLabel, 2, 0, 1, 3);
    og->setColumnStretch(0, 1);
    og->setColumnStretch(2, 1);

    // Anchor the overlay to the top of the video; a stretch fills the rest.
    auto *vlay = new QVBoxLayout(m_video);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);
    vlay->addWidget(m_overlay, 0);
    vlay->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);
    layout->addWidget(m_video, 1);

    setSelected(false);
    setControllable(true);
    setTileWidth(m_tileWidth);
    refreshOverlay();

    connect(
        this, &DeviceTile::fpsUpdated, this,
        [this](quint32 fps) {
            m_fps = fps;
            refreshOverlay();
        },
        Qt::QueuedConnection);
}

DeviceTile::~DeviceTile() = default;

void DeviceTile::setControllable(bool on)
{
    // When controllable, the tile/video receive mouse so input is forwarded to
    // the device. When not, they are mouse-transparent so the grid background can
    // drive rubber-band selection.
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    if (m_video) {
        m_video->setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    }
}

void DeviceTile::setNumber(int number)
{
    m_number = number;
    refreshOverlay();
}

void DeviceTile::setModel(const QString &model)
{
    m_model = model;
    refreshOverlay();
}

void DeviceTile::setStatusText(const QString &text)
{
    m_statusText = text;
    refreshOverlay();
}

void DeviceTile::setSelected(bool selected)
{
    m_selected = selected;
    applyBorder();
}

void DeviceTile::setUnderControl(bool on)
{
    m_underControl = on;
    setStatusText(on ? tr("under control") : QString());
    applyBorder();
}

void DeviceTile::applyBorder()
{
    // Under-control (purple) wins over click-selection (blue) over idle (grey).
    const QString color = m_underControl ? QStringLiteral("#a855f7")
                                          : (m_selected ? QStringLiteral("#3b82f6")
                                                        : QStringLiteral("#1e2636"));
    const int width = (m_underControl || m_selected) ? 2 : 1;
    setStyleSheet(QStringLiteral("DeviceTile{border:%1px solid %2; border-radius:6px; background:#0f1422;}")
                      .arg(width)
                      .arg(color));
}

void DeviceTile::setTileWidth(int width)
{
    m_tileWidth = width;
    setFixedWidth(width);
    setFixedHeight(static_cast<int>(width * m_frameAspect));
}

QSize DeviceTile::videoFrameSize() const
{
    return m_video->frameSize();
}

QSize DeviceTile::videoShowSize() const
{
    return m_video->size();
}

void DeviceTile::refreshOverlay()
{
    m_numLabel->setText(m_number > 0 ? QString::number(m_number) : QStringLiteral("·"));
    m_modelLabel->setText(m_model.isEmpty() ? tr("device") : m_model);
    m_ipLabel->setText(m_ip);
    m_fpsLabel->setText(m_fps > 0 ? QStringLiteral("%1 fps").arg(m_fps) : m_statusText);
}

void DeviceTile::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV,
                         int linesizeY, int linesizeU, int linesizeV)
{
    // Size the tile to the device's real aspect so the screen isn't stretched.
    if (width > 0 && height > 0) {
        const double aspect = static_cast<double>(height) / static_cast<double>(width);
        if (qAbs(aspect - m_frameAspect) > 0.01) {
            m_frameAspect = aspect;
            setFixedHeight(static_cast<int>(m_tileWidth * m_frameAspect));
        }
    }
    m_video->setFrameSize(QSize(width, height));
    m_video->updateTextures(dataY, dataU, dataV, static_cast<quint32>(linesizeY),
                            static_cast<quint32>(linesizeU), static_cast<quint32>(linesizeV));
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
    case QEvent::MouseButtonPress:
        m_video->setFocus();
        emit clicked(m_serial);
        emit mouseInput(m_serial, static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        emit mouseInput(m_serial, static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonDblClick:
        emit doubleClicked(m_serial);
        break;
    case QEvent::Wheel:
        emit wheelInput(m_serial, static_cast<QWheelEvent *>(event));
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        emit keyInput(m_serial, static_cast<QKeyEvent *>(event));
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
