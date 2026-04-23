#ifndef VNCCONNECTION_H
#define VNCCONNECTION_H

#include <QObject>
#include <QString>
#include <cstdint>
#include <thread>
#include <atomic>

class RfbConnection;

class VncConnection : public QObject
{
    Q_OBJECT

public:
    explicit VncConnection(QObject *parent = nullptr);
    ~VncConnection();

    void setHost(const QString &host);
    void setPort(int port);
    void setPassword(const QString &password);
    void start();
    void stop();

    bool isConnected() const;
    int framebufferWidth() const;
    int framebufferHeight() const;
    const uint8_t *framebufferData() const;

    void sendPointerEvent(int x, int y, int buttonMask);

signals:
    void connected();
    void disconnected();
    void frameUpdated();
    void errorOccurred(const QString &message);

private:
    void run();

    QString m_host;
    int m_port = 5900;
    QString m_password;
    RfbConnection *m_rfb = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

#endif // VNCCONNECTION_H
