#include "focuspanel.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "qyuvopenglwidget.h"

namespace {
QPointer<qsc::IDevice> deviceFor(const QString &serial)
{
    return qsc::IDeviceManage::getInstance().getDevice(serial);
}
}

FocusPanel::FocusPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("focusPanel");

    // --- Mirror area: video is letterboxed inside this container ---
    m_mirror = new QWidget(this);
    m_mirror->setStyleSheet("background:#000;");
    m_mirror->installEventFilter(this);

    m_video = new QYUVOpenGLWidget(m_mirror);
    m_video->setFocusPolicy(Qt::StrongFocus);
    m_video->installEventFilter(this);

    // --- Control column ---
    auto *controls = new QWidget(this);
    controls->setFixedWidth(150);

    m_title = new QLabel(controls);
    m_title->setStyleSheet("font-weight:bold;");
    auto *closeBtn = new QPushButton(tr("X"), controls);
    closeBtn->setFixedWidth(28);

    auto *header = new QHBoxLayout();
    header->addWidget(m_title, 1);
    header->addWidget(closeBtn, 0);

    auto *cv = new QVBoxLayout(controls);
    cv->setContentsMargins(8, 8, 8, 8);
    cv->setSpacing(6);
    cv->addLayout(header);

    auto addButton = [&](const QString &label) {
        auto *button = new QPushButton(label, controls);
        cv->addWidget(button);
        return button;
    };
    auto *backBtn = addButton(tr("Back"));
    auto *homeBtn = addButton(tr("Home"));
    auto *recentBtn = addButton(tr("Recent"));
    auto *notifBtn = addButton(tr("Notifications"));
    auto *volUpBtn = addButton(tr("Volume +"));
    auto *volDownBtn = addButton(tr("Volume -"));
    auto *powerBtn = addButton(tr("Power"));
    auto *shotBtn = addButton(tr("Screenshot"));
    cv->addStretch(1);

    connect(closeBtn, &QPushButton::clicked, this, [this] {
        const QString s = m_serial;
        detach();
        emit closed(s);
    });
    connect(backBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->postGoBack();
    });
    connect(homeBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->postGoHome();
    });
    connect(recentBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->postAppSwitch();
    });
    connect(notifBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->expandNotificationPanel();
    });
    connect(volUpBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->postVolumeUp();
    });
    connect(volDownBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->postVolumeDown();
    });
    connect(powerBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->postPower();
    });
    connect(shotBtn, &QPushButton::clicked, this, [this] {
        if (auto d = deviceFor(m_serial)) d->screenshot();
    });

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(m_mirror, 0);
    root->addWidget(controls, 0);

    updateSizes();
}

FocusPanel::~FocusPanel()
{
    unbindDevice();
}

void FocusPanel::showDevice(const QString &serial, const QString &title)
{
    if (m_serial == serial) {
        return;
    }
    unbindDevice();
    m_serial = serial;
    m_ratioW = 0;
    m_ratioH = 0;
    m_title->setText(title);
    if (auto d = deviceFor(serial)) {
        d->registerDeviceObserver(this);
    }
}

void FocusPanel::detach()
{
    unbindDevice();
    m_serial.clear();
}

void FocusPanel::unbindDevice()
{
    if (!m_serial.isEmpty()) {
        if (auto d = deviceFor(m_serial)) {
            d->deRegisterDeviceObserver(this);
        }
    }
}

void FocusPanel::setHostHeight(int height)
{
    m_hostHeight = height;
    updateSizes();
}

void FocusPanel::updateSizes()
{
    const double ratio =
        (m_ratioW > 0 && m_ratioH > 0) ? static_cast<double>(m_ratioW) / m_ratioH : 9.0 / 19.5;
    m_mirror->setFixedWidth(static_cast<int>(m_hostHeight * ratio));    // panel follows
    fitVideo();
}

void FocusPanel::fitVideo()
{
    if (!m_mirror || !m_video) {
        return;
    }
    const int availW = m_mirror->width();
    const int availH = m_mirror->height();
    if (availW <= 0 || availH <= 0) {
        return;
    }
    const double ratio =
        (m_ratioW > 0 && m_ratioH > 0) ? static_cast<double>(m_ratioW) / m_ratioH : 9.0 / 19.5;

    // Target the chosen host height, but never exceed the available area.
    int h = qMin(m_hostHeight, availH);
    int w = static_cast<int>(h * ratio);
    if (w > availW) {
        w = availW;
        h = static_cast<int>(w / ratio);
    }
    m_video->setGeometry((availW - w) / 2, 0, w, h);    // top-aligned
}

void FocusPanel::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV,
                         int linesizeY, int linesizeU, int linesizeV)
{
    if (width > 0 && height > 0 && (width != m_ratioW || height != m_ratioH)) {
        m_ratioW = width;
        m_ratioH = height;
        updateSizes();
    }
    m_video->setFrameSize(QSize(width, height));
    m_video->updateTextures(dataY, dataU, dataV, static_cast<quint32>(linesizeY),
                            static_cast<quint32>(linesizeU), static_cast<quint32>(linesizeV));
}

bool FocusPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_mirror) {
        if (event->type() == QEvent::Resize) {
            fitVideo();
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched != m_video) {
        return QWidget::eventFilter(watched, event);
    }
    auto device = deviceFor(m_serial);
    if (!device) {
        return QWidget::eventFilter(watched, event);
    }

    const QSize frameSize = m_video->frameSize();
    const QSize showSize = m_video->size();

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        m_video->setFocus();
        if (me->button() == Qt::RightButton) {
            device->postGoBack();
        } else if (me->button() == Qt::MiddleButton) {
            device->postGoHome();
        } else {
            device->mouseEvent(me, frameSize, showSize);
        }
        break;
    }
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::MouseButtonDblClick:
        device->mouseEvent(static_cast<QMouseEvent *>(event), frameSize, showSize);
        break;
    case QEvent::Wheel:
        device->wheelEvent(static_cast<QWheelEvent *>(event), frameSize, showSize);
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        device->keyEvent(static_cast<QKeyEvent *>(event), frameSize, showSize);
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
