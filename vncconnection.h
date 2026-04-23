#ifndef VNCCONNECTION_H
#define VNCCONNECTION_H

#include <QObject>
#include <QString>
#include <rfb/rfbclient.h>
#include <thread>
#include <atomic>

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
    rfbClient *client() const;

signals:
    void connected();
    void disconnected();
    void frameUpdated();
    void errorOccurred(const QString &message);

private:
    void run();

    static void onFinishedFrameBufferUpdate(rfbClient *cl);
    static char *onGetPassword(rfbClient *cl);

    QString m_host;
    int m_port = 5900;
    QString m_password;
    rfbClient *m_client = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

#endif // VNCCONNECTION_H
