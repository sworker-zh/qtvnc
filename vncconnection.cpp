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

void VncConnection::setHost(const QString &host)
{
    m_host = host;
}

void VncConnection::setPort(int port)
{
    m_port = port;
}

void VncConnection::setPassword(const QString &password)
{
    m_password = password;
}

bool VncConnection::isConnected() const
{
    return m_running;
}

int VncConnection::framebufferWidth() const
{
    return m_rfb ? m_rfb->framebufferWidth() : 0;
}

int VncConnection::framebufferHeight() const
{
    return m_rfb ? m_rfb->framebufferHeight() : 0;
}

const uint8_t *VncConnection::framebufferData() const
{
    return m_rfb ? m_rfb->framebufferData() : nullptr;
}

void VncConnection::sendPointerEvent(int x, int y, int buttonMask)
{
    if (m_rfb) {
        m_rfb->sendPointerEvent(x, y, buttonMask);
    }
}

void VncConnection::start()
{
    m_thread = std::thread(&VncConnection::run, this);
}

void VncConnection::stop()
{
    m_running = false;
    if (m_rfb) {
        m_rfb->disconnect();
    }
}

void VncConnection::run(){
    m_rfb = new RfbConnection();

    // Forward signals
    connect(m_rfb, &RfbConnection::connected, this, &VncConnection::connected, Qt::QueuedConnection);
    connect(m_rfb, &RfbConnection::disconnected, this, &VncConnection::disconnected, Qt::QueuedConnection);
    connect(m_rfb, &RfbConnection::frameUpdated, this, &VncConnection::frameUpdated, Qt::QueuedConnection);
    connect(m_rfb, &RfbConnection::errorOccurred, this, &VncConnection::errorOccurred, Qt::QueuedConnection);

    m_running = true;
    m_rfb->connectToHost(m_host, m_port, m_password);
    m_running = false;

    delete m_rfb;
    m_rfb = nullptr;
}
