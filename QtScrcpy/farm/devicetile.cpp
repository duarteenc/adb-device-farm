#include "devicetile.h"

#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "qyuvopenglwidget.h"

namespace {
constexpr int kHeaderHeight = 38;
constexpr double kPortraitAspect = 1.85;    // video height / width
}

DeviceTile::DeviceTile(const QString &serial, QWidget *parent)
    : QWidget(parent)
    , m_serial(serial)
    , m_statusText(tr("connecting…"))
{
    setAttribute(Qt::WA_StyledBackground, true);
    m_ip = serial.contains(':') ? serial.left(serial.indexOf(':')) : serial;

    // --- Header: number + model on top, IP + fps below ---
    m_header = new QWidget(this);
    m_header->setFixedHeight(kHeaderHeight);
    m_header->setObjectName("tileHeader");

    m_numLabel = new QLabel(m_header);
    m_numLabel->setObjectName("tileNum");
    m_modelLabel = new QLabel(m_header);
    m_modelLabel->setObjectName("tileModel");
    m_ipLabel = new QLabel(m_header);
    m_ipLabel->setObjectName("tileIp");
    m_fpsLabel = new QLabel(m_header);
    m_fpsLabel->setObjectName("tileFps");
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *hgrid = new QGridLayout(m_header);
    hgrid->setContentsMargins(6, 2, 6, 2);
    hgrid->setHorizontalSpacing(6);
    hgrid->setVerticalSpacing(0);
    hgrid->addWidget(m_numLabel, 0, 0, 2, 1);
    hgrid->addWidget(m_modelLabel, 0, 1);
    hgrid->addWidget(m_ipLabel, 1, 1);
    hgrid->addWidget(m_fpsLabel, 0, 2, 2, 1);
    hgrid->setColumnStretch(1, 1);

    // --- Video ---
    m_video = new QYUVOpenGLWidget(this);
    m_video->setFocusPolicy(Qt::StrongFocus);
    m_video->installEventFilter(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);
    layout->addWidget(m_header, 0);
    layout->addWidget(m_video, 1);

    setSelected(false);
    setTileWidth(m_tileWidth);
    refreshHeader();

    connect(
        this, &DeviceTile::fpsUpdated, this,
        [this](quint32 fps) {
            m_fps = fps;
            refreshHeader();
        },
        Qt::QueuedConnection);
}

DeviceTile::~DeviceTile() = default;

void DeviceTile::setNumber(int number)
{
    m_number = number;
    refreshHeader();
}

void DeviceTile::setModel(const QString &model)
{
    m_model = model;
    refreshHeader();
}

void DeviceTile::setStatusText(const QString &text)
{
    m_statusText = text;
    refreshHeader();
}

void DeviceTile::setSelected(bool selected)
{
    m_selected = selected;
    setStyleSheet(selected ? "DeviceTile{border:2px solid #3b82f6; border-radius:6px; background:#0f1422;}"
                           : "DeviceTile{border:1px solid #1e2636; border-radius:6px; background:#0f1422;}");
}

void DeviceTile::setTileWidth(int width)
{
    m_tileWidth = width;
    setFixedWidth(width);
    setFixedHeight(kHeaderHeight + static_cast<int>(width * kPortraitAspect));
}

QSize DeviceTile::videoFrameSize() const
{
    return m_video->frameSize();
}

QSize DeviceTile::videoShowSize() const
{
    return m_video->size();
}

void DeviceTile::refreshHeader()
{
    m_numLabel->setText(m_number > 0 ? QString::number(m_number) : QStringLiteral("·"));
    m_modelLabel->setText(m_model.isEmpty() ? tr("device") : m_model);
    m_ipLabel->setText(m_ip);
    m_fpsLabel->setText(m_fps > 0 ? QStringLiteral("%1 fps").arg(m_fps) : m_statusText);
}

void DeviceTile::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV,
                         int linesizeY, int linesizeU, int linesizeV)
{
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
    case QEvent::MouseButtonDblClick:
        emit mouseInput(m_serial, static_cast<QMouseEvent *>(event));
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
