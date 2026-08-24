#include "focuspanel.h"

#include <QKeySequence>
#include <QLineF>
#include <QPointer>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QWheelEvent>

#include "devices/deviceregistry.h"
#include "devices/deviceservice.h"
#include "farmtheme.h"
#include "mock/mockdeviceprovider.h"
#include "qyuvopenglwidget.h"

namespace farm {

namespace {
QPointer<qsc::IDevice> deviceFor(const QString &id)
{
    return qsc::IDeviceManage::getInstance().getDevice(id);
}
} // namespace

FocusPanel::FocusPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("focusPanel"));

    m_mirror = new QWidget(this);
    m_mirror->setStyleSheet(QStringLiteral("background:#000; border-radius:6px;"));
    m_mirror->installEventFilter(this);
    m_video = new QYUVOpenGLWidget(m_mirror);
    m_video->setFocusPolicy(Qt::StrongFocus);
    m_video->installEventFilter(this);

    // ---- control column ----
    auto *controls = new QWidget(this);
    controls->setObjectName(QStringLiteral("sidePanel"));
    controls->setFixedWidth(190);
    auto *cv = new QVBoxLayout(controls);
    cv->setContentsMargins(10, 8, 10, 8);
    cv->setSpacing(6);

    auto *header = new QHBoxLayout();
    m_title = new QLabel(controls);
    m_title->setStyleSheet(QStringLiteral("font-weight:bold; font-size:13px;"));
    m_title->setWordWrap(true);
    auto *closeBtn = theme::iconButton(QString(QChar(0x2715)), tr("Close host mode"), controls);
    header->addWidget(m_title, 1);
    header->addWidget(closeBtn, 0);
    cv->addLayout(header);
    m_followerLabel = theme::hint(tr("No followers"), controls);
    cv->addWidget(m_followerLabel);

    cv->addWidget(theme::sectionTitle(tr("Sync"), controls));
    m_syncMouse = new QCheckBox(tr("Mouse / touch"), controls);
    m_syncKeyboard = new QCheckBox(tr("Keyboard"), controls);
    m_syncGestures = new QCheckBox(tr("Gestures (scroll)"), controls);
    m_syncNavigation = new QCheckBox(tr("Navigation keys"), controls);
    for (QCheckBox *c : { m_syncMouse, m_syncKeyboard, m_syncGestures, m_syncNavigation }) {
        c->setChecked(true);
        cv->addWidget(c);
    }
    m_coordMode = new QComboBox(controls);
    m_coordMode->addItem(tr("Normalized coordinates"), static_cast<int>(CoordinateMode::Normalized));
    m_coordMode->addItem(tr("Aspect-aware coordinates"), static_cast<int>(CoordinateMode::AspectAware));
    m_coordMode->addItem(tr("Raw coordinates"), static_cast<int>(CoordinateMode::Raw));
    m_coordMode->setToolTip(tr("How taps on the master map onto followers with a different resolution"));
    cv->addWidget(m_coordMode);

    cv->addWidget(theme::sectionTitle(tr("Navigation"), controls));
    auto *navGrid = new QGridLayout();
    navGrid->setSpacing(4);
    auto navButton = [&](const QString &label, int row, int col, const std::function<void(qsc::IDevice *)> &fn, const QString &name) {
        auto *b = theme::button(label, controls);
        b->setMinimumHeight(28);
        connect(b, &QPushButton::clicked, this, [this, fn, name]() { navigation(fn, name); });
        navGrid->addWidget(b, row, col);
        return b;
    };
    navButton(tr("Back"), 0, 0, [](qsc::IDevice *d) { d->postGoBack(); }, QStringLiteral("back"));
    navButton(tr("Home"), 0, 1, [](qsc::IDevice *d) { d->postGoHome(); }, QStringLiteral("home"));
    navButton(tr("Recent"), 1, 0, [](qsc::IDevice *d) { d->postAppSwitch(); }, QStringLiteral("recent"));
    navButton(tr("Notif."), 1, 1, [](qsc::IDevice *d) { d->expandNotificationPanel(); }, QStringLiteral("notifications"));
    navButton(tr("Vol +"), 2, 0, [](qsc::IDevice *d) { d->postVolumeUp(); }, QStringLiteral("volume_up"));
    navButton(tr("Vol −"), 2, 1, [](qsc::IDevice *d) { d->postVolumeDown(); }, QStringLiteral("volume_down"));
    navButton(tr("Power"), 3, 0, [](qsc::IDevice *d) { d->postPower(); }, QStringLiteral("power"));
    navButton(tr("Rotate"), 3, 1, [](qsc::IDevice *d) { d->postRotate(); }, QStringLiteral("rotate"));
    navButton(tr("Wake"), 4, 0, [](qsc::IDevice *d) { d->postBackOrScreenOn(true); d->postBackOrScreenOn(false); }, QStringLiteral("wake"));
    navButton(tr("Screen off"), 4, 1, [](qsc::IDevice *d) { d->setDisplayPower(false); }, QStringLiteral("screen_off"));
    cv->addLayout(navGrid);

    cv->addWidget(theme::sectionTitle(tr("Text"), controls));
    m_textEdit = new QLineEdit(controls);
    m_textEdit->setPlaceholderText(tr("Type and press Enter…"));
    cv->addWidget(m_textEdit);
    auto *textRow = new QHBoxLayout();
    auto *sendBtn = theme::button(tr("Send"), controls, QStringLiteral("primary"));
    auto *pasteBtn = theme::button(tr("Paste PC clipboard"), controls);
    auto *tplBtn = theme::button(tr("Templates…"), controls);
    textRow->addWidget(sendBtn);
    textRow->addWidget(pasteBtn);
    cv->addLayout(textRow);
    cv->addWidget(tplBtn);

    cv->addWidget(theme::sectionTitle(tr("Actions"), controls));
    auto *shotBtn = theme::button(tr("Screenshot targets"), controls);
    auto *consoleBtn = theme::button(tr("ADB console"), controls);
    auto *apkBtn = theme::button(tr("Install APK…"), controls);
    auto *recBtn = theme::button(tr("● Record macro"), controls);
    recBtn->setCheckable(true);
    cv->addWidget(shotBtn);
    cv->addWidget(consoleBtn);
    cv->addWidget(apkBtn);
    cv->addWidget(recBtn);
    cv->addStretch(1);

    connect(closeBtn, &QPushButton::clicked, this, [this] {
        const QString s = m_serial;
        detach();
        emit closed(s);
    });
    auto sendText = [this]() {
        const QString text = m_textEdit->text();
        if (text.isEmpty()) {
            return;
        }
        forEachTarget(true, [&text](qsc::IDevice *d, const QString &) {
            QString copy = text;
            d->postTextInput(copy);
        });
        if (m_recording) {
            emit inputRecorded(QStringLiteral("text"), QVariantMap{ { QStringLiteral("text"), text } });
        }
        m_textEdit->clear();
    };
    connect(sendBtn, &QPushButton::clicked, this, sendText);
    connect(m_textEdit, &QLineEdit::returnPressed, this, sendText);
    connect(pasteBtn, &QPushButton::clicked, this, [this]() {
        forEachTarget(true, [](qsc::IDevice *d, const QString &) { d->setDeviceClipboard(true); });
    });
    connect(tplBtn, &QPushButton::clicked, this, &FocusPanel::templatesRequested);
    connect(shotBtn, &QPushButton::clicked, this, [this]() { emit screenshotRequested(targets(true)); });
    connect(consoleBtn, &QPushButton::clicked, this, [this]() { emit consoleRequested(m_serial); });
    connect(apkBtn, &QPushButton::clicked, this, [this]() { emit installApkRequested(m_serial); });
    connect(recBtn, &QPushButton::toggled, this, [this, recBtn](bool on) {
        m_recording = on;
        recBtn->setText(on ? tr("■ Stop recording") : tr("● Record macro"));
        recBtn->setObjectName(on ? QStringLiteral("danger") : QString());
        recBtn->style()->unpolish(recBtn);
        recBtn->style()->polish(recBtn);
        emit recorderToggled(on);
    });

    auto *controlsScroll = new QScrollArea(this);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScroll->setWidget(controls);
    controlsScroll->setFixedWidth(206);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(m_mirror, 0);
    root->addWidget(controlsScroll, 0);
    updateSizes();
}

FocusPanel::~FocusPanel()
{
    unbindDevice();
}

bool FocusPanel::syncMouse() const { return m_syncMouse->isChecked(); }
bool FocusPanel::syncKeyboard() const { return m_syncKeyboard->isChecked(); }
bool FocusPanel::syncGestures() const { return m_syncGestures->isChecked(); }
bool FocusPanel::syncNavigation() const { return m_syncNavigation->isChecked(); }

FocusPanel::CoordinateMode FocusPanel::coordinateMode() const
{
    return static_cast<CoordinateMode>(m_coordMode->currentData().toInt());
}

QSize FocusPanel::frameSize() const
{
    return m_video->frameSize();
}

void FocusPanel::showDevice(const QString &id, const QString &title)
{
    if (m_serial == id) {
        m_title->setText(title);
        return;
    }
    unbindDevice();
    m_serial = id;
    m_ratioW = 0;
    m_ratioH = 0;
    m_title->setText(title);
    if (MockDeviceProvider::isMockId(id)) {
        MockDeviceProvider::instance().attach(id, this);
    } else if (auto d = deviceFor(id)) {
        d->registerDeviceObserver(this);
    }
    QTimer::singleShot(60, this, [this] {
        if (auto d = deviceFor(m_serial)) {
            d->replayLastFrame(this);
        }
    });
    QTimer::singleShot(250, this, [this] {
        if (auto d = deviceFor(m_serial)) {
            d->replayLastFrame(this);
        }
    });
    setFollowers(m_followers);
}

void FocusPanel::detach()
{
    unbindDevice();
    m_serial.clear();
}

void FocusPanel::unbindDevice()
{
    if (m_serial.isEmpty()) {
        return;
    }
    if (MockDeviceProvider::isMockId(m_serial)) {
        MockDeviceProvider::instance().detach(m_serial, this);
    } else if (auto d = deviceFor(m_serial)) {
        d->deRegisterDeviceObserver(this);
    }
}

void FocusPanel::setHostHeight(int height)
{
    m_hostHeight = height;
    updateSizes();
}

void FocusPanel::setFollowers(const QStringList &ids)
{
    m_followers = ids;
    m_followers.removeAll(m_serial);
    m_followerLabel->setText(m_followers.isEmpty() ? tr("Master only — select devices to add followers")
                                                   : tr("Master + %n follower(s)", nullptr, static_cast<int>(m_followers.size())));
}

QStringList FocusPanel::targets(bool includeMaster) const
{
    QStringList list;
    if (includeMaster && !m_serial.isEmpty()) {
        list << m_serial;
    }
    list << m_followers;
    return list;
}

void FocusPanel::forEachTarget(bool includeMaster, const std::function<void(qsc::IDevice *, const QString &)> &fn)
{
    const QStringList ids = targets(includeMaster);
    for (const QString &id : ids) {
        if (auto d = deviceFor(id)) {
            fn(d, id);
        }
    }
}

void FocusPanel::navigation(const std::function<void(qsc::IDevice *)> &fn, const QString &name)
{
    if (auto master = deviceFor(m_serial)) {
        fn(master);
    }
    if (syncNavigation()) {
        for (const QString &id : m_followers) {
            if (auto d = deviceFor(id)) {
                fn(d);
            }
        }
    }
    if (m_recording) {
        emit inputRecorded(QStringLiteral("nav"), QVariantMap{ { QStringLiteral("name"), name } });
    }
}

void FocusPanel::updateSizes()
{
    const double ratio = (m_ratioW > 0 && m_ratioH > 0) ? static_cast<double>(m_ratioW) / m_ratioH : 9.0 / 19.5;
    m_mirror->setFixedWidth(static_cast<int>(m_hostHeight * ratio));
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
    const double ratio = (m_ratioW > 0 && m_ratioH > 0) ? static_cast<double>(m_ratioW) / m_ratioH : 9.0 / 19.5;
    int h = std::min(m_hostHeight, availH);
    int w = static_cast<int>(h * ratio);
    if (w > availW) {
        w = availW;
        h = static_cast<int>(w / ratio);
    }
    m_video->setGeometry((availW - w) / 2, 0, w, h);
}

void FocusPanel::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV)
{
    if (width > 0 && height > 0 && (width != m_ratioW || height != m_ratioH)) {
        m_ratioW = width;
        m_ratioH = height;
        updateSizes();
    }
    m_video->setFrameSize(QSize(width, height));
    m_video->updateTextures(dataY, dataU, dataV, static_cast<quint32>(linesizeY), static_cast<quint32>(linesizeU), static_cast<quint32>(linesizeV));
}

QPointF FocusPanel::mapForFollower(const QPointF &pos, const QSize &masterFrame, const QSize &followerFrame) const
{
    // pos is in master SHOW coordinates; the core multiplies by frame/show, so
    // for aspect-aware mapping we pre-distort pos so that the core's scaling lands
    // inside the letterboxed region of the follower.
    if (masterFrame.isEmpty() || followerFrame.isEmpty()) {
        return pos;
    }
    const double masterAspect = static_cast<double>(masterFrame.width()) / masterFrame.height();
    const double followerAspect = static_cast<double>(followerFrame.width()) / followerFrame.height();
    if (qAbs(masterAspect - followerAspect) < 0.01) {
        return pos;
    }
    const QSize show = m_video->size();
    const double nx = pos.x() / show.width();
    const double ny = pos.y() / show.height();
    double fx = nx;
    double fy = ny;
    if (followerAspect > masterAspect) {
        // follower is wider: master content is centred horizontally with bars on the sides
        const double contentW = masterAspect / followerAspect;    // fraction of follower width
        fx = (1.0 - contentW) / 2.0 + nx * contentW;
    } else {
        const double contentH = followerAspect / masterAspect;
        fy = (1.0 - contentH) / 2.0 + ny * contentH;
    }
    return QPointF(fx * show.width(), fy * show.height());
}

void FocusPanel::broadcastMouse(QMouseEvent *me)
{
    const QSize masterFrame = m_video->frameSize();
    const QSize showSize = m_video->size();
    if (auto master = deviceFor(m_serial)) {
        master->mouseEvent(me, masterFrame, showSize);
    }
    if (syncMouse()) {
        const CoordinateMode mode = coordinateMode();
        for (const QString &id : m_followers) {
            auto d = deviceFor(id);
            if (!d) {
                continue;
            }
            const QSize followerFrame = DeviceService::instance().frameSize(id);
            if (mode == CoordinateMode::Raw || followerFrame.isEmpty()) {
                d->mouseEvent(me, masterFrame, showSize);
            } else if (mode == CoordinateMode::Normalized) {
                d->mouseEvent(me, followerFrame, showSize);
            } else {
                const QPointF mapped = mapForFollower(me->position(), masterFrame, followerFrame);
                QMouseEvent copy(me->type(), mapped, me->scenePosition(), me->globalPosition(), me->button(), me->buttons(), me->modifiers());
                d->mouseEvent(&copy, followerFrame, showSize);
            }
        }
    }
    if (m_recording && !showSize.isEmpty()) {
        const QPointF n(me->position().x() / showSize.width(), me->position().y() / showSize.height());
        if (me->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
            m_pressPos = n;
            m_pressMs = QDateTime::currentMSecsSinceEpoch();
        } else if (me->type() == QEvent::MouseButtonRelease && me->button() == Qt::LeftButton) {
            const qint64 held = QDateTime::currentMSecsSinceEpoch() - m_pressMs;
            const double dist = QLineF(m_pressPos, n).length();
            QVariantMap data;
            data[QStringLiteral("x")] = m_pressPos.x();
            data[QStringLiteral("y")] = m_pressPos.y();
            if (dist > 0.03) {
                data[QStringLiteral("x2")] = n.x();
                data[QStringLiteral("y2")] = n.y();
                data[QStringLiteral("durationMs")] = static_cast<int>(held);
                emit inputRecorded(QStringLiteral("swipe"), data);
            } else if (held > 600) {
                data[QStringLiteral("durationMs")] = static_cast<int>(held);
                emit inputRecorded(QStringLiteral("longpress"), data);
            } else {
                emit inputRecorded(QStringLiteral("tap"), data);
            }
        }
    }
}

void FocusPanel::broadcastWheel(QWheelEvent *we)
{
    const QSize masterFrame = m_video->frameSize();
    const QSize showSize = m_video->size();
    if (auto master = deviceFor(m_serial)) {
        master->wheelEvent(we, masterFrame, showSize);
    }
    if (syncGestures()) {
        for (const QString &id : m_followers) {
            if (auto d = deviceFor(id)) {
                const QSize followerFrame = DeviceService::instance().frameSize(id);
                d->wheelEvent(we, coordinateMode() == CoordinateMode::Raw || followerFrame.isEmpty() ? masterFrame : followerFrame, showSize);
            }
        }
    }
}

void FocusPanel::broadcastKey(QKeyEvent *ke)
{
    const QSize masterFrame = m_video->frameSize();
    const QSize showSize = m_video->size();
    // Ctrl+V pastes the PC clipboard (push + paste, like scrcpy) instead of the raw V.
    if ((ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_V) {
        if (ke->type() == QEvent::KeyPress) {
            forEachTarget(true, [this](qsc::IDevice *d, const QString &id) {
                if (id == m_serial || syncKeyboard()) {
                    d->setDeviceClipboard(true);
                }
            });
        }
        return;
    }
    if (auto master = deviceFor(m_serial)) {
        master->keyEvent(ke, masterFrame, showSize);
    }
    if (syncKeyboard()) {
        for (const QString &id : m_followers) {
            if (auto d = deviceFor(id)) {
                d->keyEvent(ke, masterFrame, showSize);
            }
        }
    }
    if (m_recording && ke->type() == QEvent::KeyPress && !ke->text().isEmpty() && ke->text().at(0).isPrint()) {
        emit inputRecorded(QStringLiteral("text"), QVariantMap{ { QStringLiteral("text"), ke->text() } });
    } else if (m_recording && ke->type() == QEvent::KeyPress) {
        emit inputRecorded(QStringLiteral("key"), QVariantMap{ { QStringLiteral("key"), QKeySequence(ke->key()).toString() } });
    }
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
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        m_video->setFocus();
        if (me->button() == Qt::RightButton) {
            navigation([](qsc::IDevice *d) { d->postGoBack(); }, QStringLiteral("back"));
        } else if (me->button() == Qt::MiddleButton) {
            navigation([](qsc::IDevice *d) { d->postGoHome(); }, QStringLiteral("home"));
        } else {
            broadcastMouse(me);
        }
        break;
    }
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::MouseButtonDblClick:
        broadcastMouse(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::Wheel:
        broadcastWheel(static_cast<QWheelEvent *>(event));
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        broadcastKey(static_cast<QKeyEvent *>(event));
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace farm
