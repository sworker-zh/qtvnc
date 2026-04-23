#include "vncviewerwidget.h"
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QImage>

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
    if (button == Qt::LeftButton)   return 1; // rfbButton1Mask
    if (button == Qt::MiddleButton) return 2; // rfbButton2Mask
    if (button == Qt::RightButton)  return 4; // rfbButton3Mask
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

    if (!m_connection || !m_connection->framebufferData() || m_connection->framebufferWidth() == 0) {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, tr("Connecting..."));
        return;
    }

    int w = m_connection->framebufferWidth();
    int h = m_connection->framebufferHeight();
    QImage image(m_connection->framebufferData(), w, h,
                 w * 4, QImage::Format_RGB32);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    painter.drawImage(drawRect(), image);
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
    if (event->buttons() & Qt::LeftButton)   buttonMask |= 1;
    if (event->buttons() & Qt::MiddleButton) buttonMask |= 2;
    if (event->buttons() & Qt::RightButton)  buttonMask |= 4;

    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()),
        buttonMask);
}

void VncViewerWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_connected || !m_connection) return;
    QPointF remote = mapToRemote(event->position());

    int mask = event->angleDelta().y() > 0 ? 8 : 16; // button4/button5
    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()), mask);
    m_connection->sendPointerEvent(
        static_cast<int>(remote.x()), static_cast<int>(remote.y()), 0);
}

QRect VncViewerWidget::drawRect() const
{
    if (!m_connection || m_connection->framebufferWidth() == 0) return rect();
    int fbW = m_connection->framebufferWidth();
    int fbH = m_connection->framebufferHeight();

    double scaleX = static_cast<double>(width()) / fbW;
    double scaleY = static_cast<double>(height()) / fbH;
    double scale = qMin(scaleX, scaleY);

    int w = static_cast<int>(fbW * scale);
    int h = static_cast<int>(fbH * scale);
    int x = (width() - w) / 2;
    int y = (height() - h) / 2;

    return QRect(x, y, w, h);
}

QPointF VncViewerWidget::mapToRemote(QPointF localPos) const
{
    QRect dr = drawRect();
    int fbW = m_connection->framebufferWidth();
    int fbH = m_connection->framebufferHeight();
    double x = (localPos.x() - dr.x()) / dr.width() * fbW;
    double y = (localPos.y() - dr.y()) / dr.height() * fbH;
    return QPointF(qBound(0.0, x, (double)fbW),
                   qBound(0.0, y, (double)fbH));
}
