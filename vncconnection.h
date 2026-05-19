#ifndef VNCCONNECTION_H
#define VNCCONNECTION_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <thread>
#include <atomic>
#include <vector>

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

    int framebufferWidth() const;
    int framebufferHeight() const;
    const uint8_t *framebufferData() const;
    QImage takeSnapshot() const;
    void sendPointerEvent(int x, int y, int buttonMask);
    void sendKeyEvent(int key, bool pressed);

signals:
    void connected();
    void disconnected();
    void frameUpdated();
    void errorOccurred(const QString &message);

private:
    void run();
    void updateSnapshot();
    void flushPendingEvents();

    QString m_host;
    int m_port = 5900;
    QString m_password;
    RfbConnection *m_rfbConn = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;

    // Thread-safe framebuffer snapshot
    mutable QMutex m_mutex;
    std::vector<uint8_t> m_fbSnapshot;
    int m_fbSnapWidth = 0;
    int m_fbSnapHeight = 0;

    // Queued input events (flushed on worker thread)
    struct PointerEvent { int x, y, buttonMask; };
    struct KeyEvent { int key; bool pressed; };
    QMutex m_eventMutex;
    std::vector<PointerEvent> m_pendingPointerEvents;
    std::vector<KeyEvent> m_pendingKeyEvents;
};

#endif // VNCCONNECTION_H
