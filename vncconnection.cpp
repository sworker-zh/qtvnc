#include "vncconnection.h"
#include "rfbconnection.h"
#include <QImage>

VncConnection::VncConnection(QObject *parent)
    : QObject(parent)
{
}

VncConnection::~VncConnection()
{
    stop();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void VncConnection::setHost(const QString &host) { m_host = host; }
void VncConnection::setPort(int port) { m_port = port; }
void VncConnection::setPassword(const QString &password) { m_password = password; }

int VncConnection::framebufferWidth() const
{
    QMutexLocker locker(&m_mutex);
    return m_fbSnapWidth;
}

int VncConnection::framebufferHeight() const
{
    QMutexLocker locker(&m_mutex);
    return m_fbSnapHeight;
}

const uint8_t *VncConnection::framebufferData() const
{
    QMutexLocker locker(&m_mutex);
    return m_fbSnapshot.data();
}

QImage VncConnection::takeSnapshot() const
{
    QMutexLocker locker(&m_mutex);
    if (m_fbSnapWidth <= 0 || m_fbSnapHeight <= 0 || m_fbSnapshot.empty())
        return QImage();
    return QImage(m_fbSnapshot.data(), m_fbSnapWidth, m_fbSnapHeight,
                  m_fbSnapWidth * 4, QImage::Format_RGB32).copy();
}

void VncConnection::sendPointerEvent(int x, int y, int buttonMask)
{
    if (!m_running) return;
    QMutexLocker locker(&m_eventMutex);
    m_pendingPointerEvents.push_back({x, y, buttonMask});
}

void VncConnection::sendKeyEvent(int key, bool pressed)
{
    if (!m_running) return;
    QMutexLocker locker(&m_eventMutex);
    m_pendingKeyEvents.push_back({key, pressed});
}

void VncConnection::start()
{
    m_thread = std::thread(&VncConnection::run, this);
}

void VncConnection::stop()
{
    m_running = false;
    if (m_rfbConn) m_rfbConn->disconnect();
}

void VncConnection::updateSnapshot()
{
    QMutexLocker locker(&m_mutex);
    m_fbSnapWidth = m_rfbConn->framebufferWidth();
    m_fbSnapHeight = m_rfbConn->framebufferHeight();
    const uint8_t *data = m_rfbConn->framebufferData();
    if (data && m_fbSnapWidth > 0 && m_fbSnapHeight > 0) {
        m_fbSnapshot.assign(data, data + (size_t)m_fbSnapWidth * m_fbSnapHeight * 4);
    }
}

void VncConnection::flushPendingEvents()
{
    QMutexLocker locker(&m_eventMutex);
    for (auto &ev : m_pendingPointerEvents) {
        m_rfbConn->sendPointerEvent(ev.x, ev.y, ev.buttonMask);
    }
    m_pendingPointerEvents.clear();
    for (auto &ev : m_pendingKeyEvents) {
        m_rfbConn->sendKeyEvent(ev.key, ev.pressed);
    }
    m_pendingKeyEvents.clear();
}

void VncConnection::run()
{
    m_rfbConn = new RfbConnection();

    // Forward signals via DirectConnection so snapshot copy happens on worker thread
    connect(m_rfbConn, &RfbConnection::connected, this, [this]() {
        updateSnapshot();
        emit connected();
    }, Qt::DirectConnection);
    connect(m_rfbConn, &RfbConnection::disconnected, this, [this]() {
        m_running = false;
        emit disconnected();
    }, Qt::DirectConnection);
    connect(m_rfbConn, &RfbConnection::frameUpdated, this, [this]() {
        updateSnapshot();
        emit frameUpdated();
    }, Qt::DirectConnection);
    connect(m_rfbConn, &RfbConnection::errorOccurred, this, [this](const QString &msg) {
        m_running = false;
        emit errorOccurred(msg);
    }, Qt::DirectConnection);

    // Set idle callback so worker thread can flush queued input events
    m_rfbConn->setIdleCallback([this]() { flushPendingEvents(); });

    m_running = true;
    m_rfbConn->connectToHost(m_host, m_port, m_password);

    // Worker thread: safe to delete directly (no event loop)
    delete m_rfbConn;
    m_rfbConn = nullptr;
    m_running = false;
}
