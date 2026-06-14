#include "devicetile.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
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

    // Setup spinner animation
    m_spinnerAnimation = new QPropertyAnimation(this, "spinnerRotation", this);
    m_spinnerAnimation->setDuration(1000);
    m_spinnerAnimation->setStartValue(0);
    m_spinnerAnimation->setEndValue(360);
    m_spinnerAnimation->setLoopCount(-1);
    connect(m_spinnerAnimation, &QPropertyAnimation::valueChanged, this, [this]() {
        update();
    });

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

void DeviceTile::setLoading(bool loading)
{
    m_loading = loading;
    if (loading) {
        m_spinnerAnimation->start();
    } else {
        m_spinnerAnimation->stop();
    }
    update();
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

void DeviceTile::setSelectionPreview(bool preview)
{
    m_selectionPreview = preview;
    applyBorder();
}

void DeviceTile::applyBorder()
{
    // Priority: under-control (purple) > selection-preview (light green) > click-selection (green) > idle (grey)
    QString color;
    int width;
    QString background = QStringLiteral("#0f1422");

    if (m_underControl) {
        color = QStringLiteral("#a855f7");  // purple
        width = 12;
    } else if (m_selectionPreview) {
        color = QStringLiteral("#4ade80");  // light green
        width = 9;
        background = QStringLiteral("rgba(34, 197, 94, 0.1)");  // slight green tint
    } else if (m_selected) {
        color = QStringLiteral("#22c55e");  // green
        width = 12;
    } else {
        color = QStringLiteral("#1e2636");  // grey
        width = 1;
    }

    setStyleSheet(QStringLiteral("DeviceTile{border:%1px solid %2; background:%3;}")
                      .arg(width)
                      .arg(color)
                      .arg(background));
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
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            showContextMenu(mouseEvent->globalPosition().toPoint());
            return true;
        }
        m_video->setFocus();
        emit clicked(m_serial);
        emit mouseInput(m_serial, mouseEvent);
        break;
    }
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

void DeviceTile::contextMenuEvent(QContextMenuEvent *event)
{
    showContextMenu(event->globalPos());
}

void DeviceTile::showContextMenu(const QPoint &pos)
{
    // Delegate to FarmWindow to handle both single and multi-selection menus
    emit contextMenuRequested(m_serial, pos);
}

void DeviceTile::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    if (!m_loading) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw GenFarmer-style 4-square spinner in the center of the tile
    const int centerX = width() / 2;
    const int centerY = height() / 2;
    const int squareSize = 18;
    const int spacing = 6;

    painter.save();
    painter.translate(centerX, centerY);
    painter.rotate(m_spinnerRotation);

    // Draw 4 squares in a 2x2 grid
    QPen pen(QColor(59, 130, 246), 2);  // Blue color (#3b82f6)
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const int halfSize = squareSize / 2;
    const int halfSpacing = spacing / 2;

    // Top-left square
    painter.drawRect(-halfSize - halfSpacing, -halfSize - halfSpacing, squareSize, squareSize);
    // Top-right square
    painter.drawRect(halfSpacing, -halfSize - halfSpacing, squareSize, squareSize);
    // Bottom-left square
    painter.drawRect(-halfSize - halfSpacing, halfSpacing, squareSize, squareSize);
    // Bottom-right square
    painter.drawRect(halfSpacing, halfSpacing, squareSize, squareSize);

    painter.restore();
}
