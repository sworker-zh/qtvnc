#include "vncconnection.h"
#include <cstring>

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

rfbClient *VncConnection::client() const
{
    return m_client;
}

void VncConnection::start()
{
    m_thread = std::thread(&VncConnection::run, this);
}

void VncConnection::stop()
{
    m_running = false;
}

void VncConnection::run()
{
    m_client = rfbGetClient(8, 3, 4);

    m_client->format.depth = 16;
    m_client->format.bitsPerPixel = 16;
    m_client->format.redShift = 11;
    m_client->format.greenShift = 5;
    m_client->format.blueShift = 0;
    m_client->format.redMax = 0x1f;
    m_client->format.greenMax = 0x3f;
    m_client->format.blueMax = 0x1f;

    m_client->appData.encodingsString = "tight ultra hextile zlib corre rre raw";
    m_client->appData.compressLevel = 9;
    m_client->appData.qualityLevel = 5;
    m_client->appData.useRemoteCursor = TRUE;

    rfbClientSetClientData(m_client, nullptr, this);
    m_client->FinishedFrameBufferUpdate = onFinishedFrameBufferUpdate;
    if (!m_password.isEmpty()) {
        m_client->GetPassword = onGetPassword;
    }

    m_client->serverHost = strdup(m_host.toUtf8().constData());
    m_client->serverPort = m_port;

    m_running = true;

    if (!rfbInitClient(m_client, 0, nullptr)) {
        m_client = nullptr;
        emit errorOccurred(tr("Connection failed"));
        return;
    }

    emit connected();

    while (m_running) {
        int result = WaitForMessage(m_client, 500);
        if (result < 0) {
            emit errorOccurred(tr("Connection lost"));
            break;
        }
        if (result > 0) {
            if (!HandleRFBServerMessage(m_client)) {
                emit errorOccurred(tr("Server disconnected"));
                break;
            }
        }
    }

    rfbClientCleanup(m_client);
    m_client = nullptr;
    m_running = false;
    emit disconnected();
}

void VncConnection::onFinishedFrameBufferUpdate(rfbClient *cl)
{
    auto *self = static_cast<VncConnection *>(rfbClientGetClientData(cl, nullptr));
    if (self) {
        emit self->frameUpdated();
    }
}

char *VncConnection::onGetPassword(rfbClient *cl)
{
    auto *self = static_cast<VncConnection *>(rfbClientGetClientData(cl, nullptr));
    if (!self || self->m_password.isEmpty()) {
        return nullptr;
    }
    return strdup(self->m_password.toUtf8().constData());
}
