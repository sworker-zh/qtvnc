#ifndef RFBCONNECTION_H
#define RFBCONNECTION_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTcpSocket>
#include <vector>
#include <atomic>
#include "rfbprotocol.h"

#include <zlib.h>

class RfbConnection : public QObject
{
    Q_OBJECT

public:
    explicit RfbConnection(QObject *parent = nullptr);
    ~RfbConnection();

    void connectToHost(const QString &host, int port, const QString &password);
    void disconnect();
    void sendPointerEvent(int x, int y, int buttonMask);

    int framebufferWidth() const;
    int framebufferHeight() const;
    const uint8_t *framebufferData() const;

signals:
    void connected();
    void disconnected();
    void frameUpdated();
    void errorOccurred(const QString &message);

private:
    bool readExact(char *buf, int len);
    bool writeExact(const char *buf, int len);

    // Protocol handshake
    bool negotiateVersion();
    bool negotiateSecurity();
    bool authenticate();
    bool clientInit();
    bool serverInit();
    bool setEncodings();
    bool requestFramebufferUpdate(bool incremental);

    // Message handling
    bool handleServerMessage();
    bool handleFramebufferUpdate();

    // Encoding handlers
    bool handleRawEncoding(int x, int y, int w, int h);
    bool handleCopyRectEncoding(int x, int y, int w, int h);
    bool handleHextileEncoding(int x, int y, int w, int h);
    bool handleZlibEncoding(int x, int y, int w, int h);
    bool handleTightEncoding(int x, int y, int w, int h);
    bool handleDesktopSizeEncoding(int x, int y, int w, int h);
    bool handleCursorEncoding(int x, int y, int w, int h);
    bool handleSetColourMap();
    bool handleBell();
    bool handleServerCutText();

    // Tight helpers
    int  tightReadCompactLen();
    bool tightDecompress(int streamId, const char *compressed, int compressedLen,
                         char *out, int outLen);
    bool tightFilterCopy(const char *src, int srcLen, int x, int y, int w, int h);
    bool tightFilterPalette(const char *src, int srcLen, int x, int y, int w, int h, int paletteSize);
    bool tightFilterGradient(const char *src, int srcLen, int x, int y, int w, int h);

    // zlib stream cleanup
    void cleanupZlib();

    QTcpSocket *m_socket = nullptr;
    QString m_host;
    int m_port = 5900;
    QString m_password;
    int m_serverMajor = 0;
    int m_serverMinor = 0;

    // Framebuffer
    std::vector<uint8_t> m_framebuffer;
    int m_fbWidth = 0;
    int m_fbHeight = 0;
    RfbPixelFormat m_pixelFormat = {};

    std::atomic<bool> m_running{false};
    bool m_pendingUpdateRequest = false;
    bool m_tightCutZeros = false;  // Tight optimization: 3 bytes/pixel for 24-bit depth

    // Zlib state (shared by Zlib and ZRLE encodings)
    z_stream m_decompStream = {};
    bool m_decompStreamInited = false;

    // Tight state (4 independent zlib streams)
    z_stream m_tightStreams[4] = {};
    bool m_tightStreamActive[4] = {};

    // Tight working buffers
    std::vector<uint8_t> m_tightPalette;  // palette for current rect
    std::vector<uint8_t> m_tightPrevRow;  // previous row for gradient filter
};

#endif // RFBCONNECTION_H
