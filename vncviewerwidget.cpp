#include "vncviewerwidget.h"
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMutex>

// Mouse button masks (RFB protocol)
static constexpr int RFB_BUTTON1 = 1;
static constexpr int RFB_BUTTON2 = 2;
static constexpr int RFB_BUTTON3 = 4;
static constexpr int RFB_BUTTON4 = 8;
static constexpr int RFB_BUTTON5 = 16;

// Map Qt::Key to X11 keysym for RFB key events
static uint32_t qtKeyToX11Key(int qtKey)
{
    // ASCII printable range maps directly
    if (qtKey >= 0x20 && qtKey <= 0x7E)
        return qtKey;

    // Special keys
    switch (qtKey) {
    case Qt::Key_Backspace:  return 0xFF08;
    case Qt::Key_Tab:        return 0xFF09;
    case Qt::Key_Return:     return 0xFF0D;
    case Qt::Key_Enter:      return 0xFF0D;
    case Qt::Key_Escape:     return 0xFF1B;
    case Qt::Key_Insert:     return 0xFF63;
    case Qt::Key_Delete:     return 0xFFFF;
    case Qt::Key_Home:       return 0xFF50;
    case Qt::Key_End:        return 0xFF57;
    case Qt::Key_PageUp:     return 0xFF55;
    case Qt::Key_PageDown:   return 0xFF56;
    case Qt::Key_Left:       return 0xFF51;
    case Qt::Key_Up:         return 0xFF52;
    case Qt::Key_Right:      return 0xFF53;
    case Qt::Key_Down:       return 0xFF54;
    case Qt::Key_F1:         return 0xFFBE;
    case Qt::Key_F2:         return 0xFFBF;
    case Qt::Key_F3:         return 0xFFC0;
    case Qt::Key_F4:         return 0xFFC1;
    case Qt::Key_F5:         return 0xFFC2;
    case Qt::Key_F6:         return 0xFFC3;
    case Qt::Key_F7:         return 0xFFC4;
    case Qt::Key_F8:         return 0xFFC5;
    case Qt::Key_F9:         return 0xFFC6;
    case Qt::Key_F10:        return 0xFFC7;
    case Qt::Key_F11:        return 0xFFC8;
    case Qt::Key_F12:        return 0xFFC9;
    case Qt::Key_Space:      return 0x0020;
    default:                 return qtKey;
    }
}

VncViewerWidget::VncViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setupContextMenu();
}

VncViewerWidget::~VncViewerWidget()
{
    if (m_connection) {
        m_connection->stop();
        delete m_connection;
    }
}

void VncViewerWidget::setupContextMenu()
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *fullScreenAction = menu.addAction(
            isFullScreen() ? tr("Exit Fullscreen") : tr("Fullscreen"));
        menu.addSeparator();
        QAction *exitAction = menu.addAction(tr("Exit"));

        QAction *chosen = menu.exec(mapToGlobal(pos));
        if (chosen == fullScreenAction) {
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
        } else if (chosen == exitAction) {
            close();
        }
    });
}

QPointF VncViewerWidget::mousePos(QMouseEvent *event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

int VncViewerWidget::mapMouseButton(Qt::MouseButton button) const
{
    if (button == Qt::LeftButton)   return RFB_BUTTON1;
    if (button == Qt::MiddleButton) return RFB_BUTTON2;
    if (button == Qt::RightButton)  return RFB_BUTTON3;
    return 0;
}

void VncViewerWidget::startConnection(const QString &host, int port, const QString &password)
{
    m_connection = new VncConnection();

    connect(m_connection, &VncConnection::frameUpdated,
            this, &VncViewerWidget::onFrameUpdated, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::connected,
            this, &VncViewerWidget::onConnected, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::disconnected,
            this, &VncViewerWidget::onDisconnected, Qt::QueuedConnection);
    connect(m_connection, &VncConnection::errorOccurred,
            this, &VncViewerWidget::onError, Qt::QueuedConnection);

    m_connection->setHost(host);
    m_connection->setPort(port);
    m_connection->setPassword(password);
    m_connection->start();
}

void VncViewerWidget::onFrameUpdated()
{
    update();
}

void VncViewerWidget::onConnected()
{
    m_connected = true;
    if (m_connection) {
        QMutexLocker locker(&m_connection->snapshotMutex());
        resize(m_connection->framebufferWidth(), m_connection->framebufferHeight());
    }
}

void VncViewerWidget::onDisconnected()
{
    m_connected = false;
    emit connectionLost();
}

void VncViewerWidget::onError(const QString &message)
{
    m_connected = false;
    QMessageBox::warning(this, tr("VNC Error"), message);
    emit connectionLost();
}

void VncViewerWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (!m_connection) {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, tr("Connecting..."));
        return;
    }

    // Copy framebuffer snapshot under mutex, then paint from local copy
    QImage frameImage;
    {
        QMutexLocker locker(&m_connection->snapshotMutex());
        int w = m_connection->framebufferWidth();
        int h = m_connection->framebufferHeight();
        const uint8_t *data = m_connection->framebufferData();
        if (data && w > 0 && h > 0) {
            frameImage = QImage(data, w, h, w * 4, QImage::Format_RGB32).copy();
        }
    }

    if (frameImage.isNull()) {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, tr("Connecting..."));
        return;
    }

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.drawImage(drawRect(), frameImage);
}

void VncViewerWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_connected || !m_connection) return;
    QPointF remote = mapToRemote(mousePos(event));
    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()),
        mapMouseButton(event->button()));
}

void VncViewerWidget::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!m_connected || !m_connection) return;
    QPointF remote = mapToRemote(mousePos(event));
    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()), 0);
}

void VncViewerWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_connected || !m_connection) return;
    QPointF remote = mapToRemote(mousePos(event));

    int buttonMask = 0;
    if (event->buttons() & Qt::LeftButton)   buttonMask |= RFB_BUTTON1;
    if (event->buttons() & Qt::MiddleButton) buttonMask |= RFB_BUTTON2;
    if (event->buttons() & Qt::RightButton)  buttonMask |= RFB_BUTTON3;

    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()), buttonMask);
}

void VncViewerWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_connected || !m_connection) return;
    event->accept();
    uint32_t keysym = qtKeyToX11Key(event->key());
    m_connection->sendKeyEvent(keysym, true);
}

void VncViewerWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_connected || !m_connection) return;
    event->accept();
    uint32_t keysym = qtKeyToX11Key(event->key());
    m_connection->sendKeyEvent(keysym, false);
}

void VncViewerWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_connected || !m_connection) return;
    QPointF remote = mapToRemote(event->position());

    int mask = event->angleDelta().y() > 0 ? RFB_BUTTON4 : RFB_BUTTON5;
    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()), mask);
    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()), 0);
}

QRect VncViewerWidget::drawRect() const
{
    if (!m_connection || m_connection->framebufferWidth() == 0) return rect();
    int fw = m_connection->framebufferWidth();
    int fh = m_connection->framebufferHeight();

    double scaleX = static_cast<double>(width()) / fw;
    double scaleY = static_cast<double>(height()) / fh;
    double scale = qMin(scaleX, scaleY);

    int w = static_cast<int>(fw * scale);
    int h = static_cast<int>(fh * scale);
    int x = (width() - w) / 2;
    int y = (height() - h) / 2;

    return QRect(x, y, w, h);
}

QPointF VncViewerWidget::mapToRemote(QPointF localPos) const
{
    QRect dr = drawRect();
    int fw = m_connection->framebufferWidth();
    int fh = m_connection->framebufferHeight();
    double x = (localPos.x() - dr.x()) / dr.width() * fw;
    double y = (localPos.y() - dr.y()) / dr.height() * fh;
    return QPointF(qBound(0.0, x, (double)fw), qBound(0.0, y, (double)fh));
}
