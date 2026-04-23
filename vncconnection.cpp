#include "vncconnection.h"
#include "rfbconnection.h"

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

RfbConnection *VncConnection::rfbConn() const { return m_rfbConn; }

int VncConnection::framebufferWidth() const
{
    return m_rfbConn ? m_rfbConn->framebufferWidth() : 0;
}

int VncConnection::framebufferHeight() const
{
    return m_rfbConn ? m_rfbConn->framebufferHeight() : 0;
}

const uint8_t *VncConnection::framebufferData() const
{
    return m_rfbConn ? m_rfbConn->framebufferData() : nullptr;
}

void VncConnection::sendPointerEvent(int x, int y, int buttonMask)
{
    if (m_rfbConn) m_rfbConn->sendPointerEvent(x, y, buttonMask);
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

void VncConnection::run()
{
    m_rfbConn = new RfbConnection();

    // Forward RfbConnection signals through VncConnection (cross-thread)
    connect(m_rfbConn, &RfbConnection::connected, this, [this]() {
        emit connected();
    });
    connect(m_rfbConn, &RfbConnection::disconnected, this, [this]() {
        m_running = false;
        emit disconnected();
    });
    connect(m_rfbConn, &RfbConnection::frameUpdated, this, [this]() {
        emit frameUpdated();
    });
    connect(m_rfbConn, &RfbConnection::errorOccurred, this, [this](const QString &msg) {
        m_running = false;
        emit errorOccurred(msg);
    });

    m_running = true;
    m_rfbConn->connectToHost(m_host, m_port, m_password);

    // connectToHost blocks until disconnected
    m_rfbConn->deleteLater();
    m_rfbConn = nullptr;
    m_running = false;
}
