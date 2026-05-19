#include "rfbconnection.h"
#include "rfbcrypto.h"
#include <QTcpSocket>
#include <QImage>
#include <QDebug>
#include <cstring>
#include <algorithm>

RfbConnection::RfbConnection(QObject *parent)
    : QObject(parent)
{
    memset(&m_decompStream, 0, sizeof(m_decompStream));
    for (int i = 0; i < 4; i++) {
        memset(&m_tightStreams[i], 0, sizeof(m_tightStreams[i]));
        m_tightStreamActive[i] = false;
    }
}

RfbConnection::~RfbConnection()
{
    disconnect();
}

int RfbConnection::framebufferWidth() const { return m_fbWidth; }
int RfbConnection::framebufferHeight() const { return m_fbHeight; }
const uint8_t *RfbConnection::framebufferData() const { return m_framebuffer.data(); }

bool RfbConnection::validateRect(int x, int y, int w, int h) const
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
    if (x + w > m_fbWidth || y + h > m_fbHeight) return false;
    // Check for integer overflow in row offset calculation
    if (static_cast<long long>(y) * m_fbWidth + x > INT_MAX / 4) return false;
    return true;
}

void RfbConnection::cleanupZlib()
{
    if (m_decompStreamInited) {
        inflateEnd(&m_decompStream);
        m_decompStreamInited = false;
    }
    for (int i = 0; i < 4; i++) {
        if (m_tightStreamActive[i]) {
            inflateEnd(&m_tightStreams[i]);
            m_tightStreamActive[i] = false;
        }
    }
}

bool RfbConnection::readExact(char *buf, int len)
{
    int total = 0;
    while (total < len) {
        qint64 n = m_socket->read(buf + total, len - total);
        if (n < 0) return false;
        if (n == 0) {
            if (!m_socket->waitForReadyRead(5000)) return false;
            continue;
        }
        total += n;
    }
    return true;
}

bool RfbConnection::writeExact(const char *buf, int len)
{
    int total = 0;
    while (total < len) {
        qint64 n = m_socket->write(buf + total, len - total);
        if (n < 0) return false;
        total += n;
    }
    return m_socket->waitForBytesWritten(5000);
}

// --- Protocol handshake ---

bool RfbConnection::negotiateVersion()
{
    char serverVersion[13];
    if (!readExact(serverVersion, 12)) return false;
    serverVersion[12] = '\0';

    if (sscanf(serverVersion, "RFB %03d.%03d\n", &m_serverMajor, &m_serverMinor) != 2)
        return false;

    int ourMinor = 8;
    if (m_serverMajor == 3 && m_serverMinor < 8)
        ourMinor = m_serverMinor;

    char clientVersion[13];
    snprintf(clientVersion, sizeof(clientVersion), "RFB %03d.%03d\n", 3, ourMinor);
    if (!writeExact(clientVersion, 12)) return false;

    m_serverMinor = ourMinor;
    return true;
}

bool RfbConnection::negotiateSecurity()
{
    if (m_serverMinor >= 7) {
        uint8_t count;
        if (!readExact((char *)&count, 1)) return false;
        if (count == 0) return false;

        QByteArray types(count, 0);
        if (!readExact(types.data(), count)) return false;

        uint8_t chosen = RFB_SECURITY_INVALID;
        bool hasNone = false;
        for (int i = 0; i < count; i++) {
            if ((uint8_t)types[i] == RFB_SECURITY_VNCAUTH)
                chosen = RFB_SECURITY_VNCAUTH;
            if ((uint8_t)types[i] == RFB_SECURITY_NONE)
                hasNone = true;
        }
        if (chosen == RFB_SECURITY_INVALID) {
            if (hasNone) chosen = RFB_SECURITY_NONE;
            else return false;
        }

        if (!writeExact((const char *)&chosen, 1)) return false;
        if (chosen == RFB_SECURITY_VNCAUTH) {
            if (!authenticate()) return false;
        }
        if (m_serverMinor >= 8 && chosen == RFB_SECURITY_NONE) {
            uint32_t result;
            if (!readExact((char *)&result, 4)) return false;
        }
    } else {
        uint32_t secType;
        if (!readExact((char *)&secType, 4)) return false;
        if (secType == 0) return false;
        if (secType == RFB_SECURITY_VNCAUTH) {
            if (!authenticate()) return false;
        }
    }
    return true;
}

bool RfbConnection::authenticate()
{
    QByteArray challenge(16, 0);
    if (!readExact(challenge.data(), 16)) return false;

    QByteArray response = vncEncryptChallenge(challenge, m_password.toUtf8());
    if (!writeExact(response.constData(), 16)) return false;

    uint32_t result;
    if (!readExact((char *)&result, 4)) return false;
    if (result != 0) return false;
    return true;
}

bool RfbConnection::clientInit()
{
    uint8_t shared = 1;
    return writeExact((const char *)&shared, 1);
}

bool RfbConnection::serverInit()
{
    char buf[24];
    if (!readExact(buf, 24)) return false;

    m_fbWidth  = (uint8_t)buf[0] << 8 | (uint8_t)buf[1];
    m_fbHeight = (uint8_t)buf[2] << 8 | (uint8_t)buf[3];
    m_pixelFormat.bitsPerPixel = buf[4];
    m_pixelFormat.depth        = buf[5];
    m_pixelFormat.bigEndian    = buf[6];
    m_pixelFormat.trueColour   = buf[7];
    m_pixelFormat.redMax   = (uint8_t)buf[8]  << 8 | (uint8_t)buf[9];
    m_pixelFormat.greenMax = (uint8_t)buf[10] << 8 | (uint8_t)buf[11];
    m_pixelFormat.blueMax  = (uint8_t)buf[12] << 8 | (uint8_t)buf[13];
    m_pixelFormat.redShift   = buf[14];
    m_pixelFormat.greenShift = buf[15];
    m_pixelFormat.blueShift  = buf[16];

    uint32_t nameLen = (uint8_t)buf[20] << 24 | (uint8_t)buf[21] << 16 |
                       (uint8_t)buf[22] << 8  | (uint8_t)buf[23];
    if (nameLen > 4096) nameLen = 4096;
    QByteArray name(nameLen, 0);
    if (nameLen > 0 && !readExact(name.data(), nameLen)) return false;

    // Request 32bpp BGRX (native for QImage::Format_RGB32)
    char pfMsg[20];
    pfMsg[0] = RFB_MSG_SET_PIXEL_FORMAT;
    memset(pfMsg + 1, 0, 3);
    pfMsg[4]  = 32;  pfMsg[5]  = 24;
    pfMsg[6]  = 0;   pfMsg[7]  = 1;
    pfMsg[8]  = 0;   pfMsg[9]  = 255;
    pfMsg[10] = 0;   pfMsg[11] = 255;
    pfMsg[12] = 0;   pfMsg[13] = 255;
    pfMsg[14] = 16;  pfMsg[15] = 8;   pfMsg[16] = 0;
    memset(pfMsg + 17, 0, 3);
    if (!writeExact(pfMsg, 20)) return false;

    // Check for zero or overflow in framebuffer size
    if (m_fbWidth <= 0 || m_fbHeight <= 0) return false;
    if (static_cast<long long>(m_fbWidth) * m_fbHeight * 4 > INT_MAX) return false;

    m_framebuffer.resize(m_fbWidth * m_fbHeight * 4, 0);

    // Tight cutZeros: our requested format is 32bpp depth 24 with all max 255
    m_tightCutZeros = true;

    return true;
}

bool RfbConnection::setEncodings()
{
    int32_t encodings[] = {
        RFB_ENCODING_TIGHT,
        RFB_ENCODING_ZLIB,
        RFB_ENCODING_HEXTILE,
        RFB_ENCODING_COPYRECT,
        RFB_ENCODING_RAW,
        RFB_ENCODING_DESKTOPSIZE,
        RFB_ENCODING_CURSOR,
    };
    int count = sizeof(encodings) / sizeof(encodings[0]);

    char msg[4 + count * 4];
    msg[0] = RFB_MSG_SET_ENCODINGS;
    msg[1] = 0;
    msg[2] = (char)(count >> 8);
    msg[3] = (char)(count & 0xFF);
    for (int i = 0; i < count; i++) {
        msg[4 + i * 4 + 0] = (char)((encodings[i] >> 24) & 0xFF);
        msg[4 + i * 4 + 1] = (char)((encodings[i] >> 16) & 0xFF);
        msg[4 + i * 4 + 2] = (char)((encodings[i] >> 8) & 0xFF);
        msg[4 + i * 4 + 3] = (char)(encodings[i] & 0xFF);
    }
    return writeExact(msg, 4 + count * 4);
}

bool RfbConnection::requestFramebufferUpdate(bool incremental)
{
    if (m_pendingUpdateRequest) return true;
    m_pendingUpdateRequest = true;

    char msg[10];
    msg[0] = RFB_MSG_FRAMEBUFFER_UPDATE_REQ;
    msg[1] = incremental ? 1 : 0;
    msg[2] = 0; msg[3] = 0;
    msg[4] = 0; msg[5] = 0;
    msg[6] = (char)(m_fbWidth >> 8);
    msg[7] = (char)(m_fbWidth & 0xFF);
    msg[8] = (char)(m_fbHeight >> 8);
    msg[9] = (char)(m_fbHeight & 0xFF);
    return writeExact(msg, 10);
}

void RfbConnection::sendPointerEvent(int x, int y, int buttonMask)
{
    char msg[6];
    msg[0] = RFB_MSG_POINTER_EVENT;
    msg[1] = (char)(buttonMask & 0xFF);
    msg[2] = (char)(x >> 8);   msg[3] = (char)(x & 0xFF);
    msg[4] = (char)(y >> 8);   msg[5] = (char)(y & 0xFF);
    writeExact(msg, 6);
}

// --- Server message dispatch ---

bool RfbConnection::handleServerMessage()
{
    uint8_t msgType;
    if (!readExact((char *)&msgType, 1)) return false;

    switch (msgType) {
    case RFB_MSG_FRAMEBUFFER_UPDATE: return handleFramebufferUpdate();
    case RFB_MSG_SET_COLOUR_MAP:     return handleSetColourMap();
    case RFB_MSG_BELL:                return handleBell();
    case RFB_MSG_SERVER_CUT_TEXT:     return handleServerCutText();
    default: return false;
    }
}

bool RfbConnection::handleFramebufferUpdate()
{
    char header[3];
    if (!readExact(header, 3)) return false;
    int numRects = (uint8_t)header[1] << 8 | (uint8_t)header[2];

    for (int i = 0; i < numRects; i++) {
        char rh[12];
        if (!readExact(rh, 12)) return false;

        int x = (uint8_t)rh[0] << 8 | (uint8_t)rh[1];
        int y = (uint8_t)rh[2] << 8 | (uint8_t)rh[3];
        int w = (uint8_t)rh[4] << 8 | (uint8_t)rh[5];
        int h = (uint8_t)rh[6] << 8 | (uint8_t)rh[7];
        int32_t enc = ((int32_t)(uint8_t)rh[8] << 24) |
                      ((int32_t)(uint8_t)rh[9] << 16) |
                      ((int32_t)(uint8_t)rh[10] << 8) |
                      (int32_t)(uint8_t)rh[11];

        bool ok = false;
        switch (enc) {
        case RFB_ENCODING_RAW:         ok = validateRect(x, y, w, h) && handleRawEncoding(x, y, w, h); break;
        case RFB_ENCODING_COPYRECT:    ok = validateRect(x, y, w, h) && handleCopyRectEncoding(x, y, w, h); break;
        case RFB_ENCODING_HEXTILE:     ok = validateRect(x, y, w, h) && handleHextileEncoding(x, y, w, h); break;
        case RFB_ENCODING_ZLIB:        ok = validateRect(x, y, w, h) && handleZlibEncoding(x, y, w, h); break;
        case RFB_ENCODING_TIGHT:       ok = validateRect(x, y, w, h) && handleTightEncoding(x, y, w, h); break;
        case RFB_ENCODING_DESKTOPSIZE: ok = handleDesktopSizeEncoding(x, y, w, h); break;
        case RFB_ENCODING_CURSOR:      ok = handleCursorEncoding(x, y, w, h); break;
        default:
            qWarning() << "[VNC] Unknown encoding:" << enc;
            ok = false; break;
        }
        if (!ok) {
            qWarning() << "[VNC] Encoding" << enc << "failed at rect" << x << y << w << h;
            return false;
        }
    }

    emit frameUpdated();
    return true;
}

// --- Encoding handlers ---

bool RfbConnection::handleRawEncoding(int x, int y, int w, int h)
{
    int bytesPerRow = w * 4;
    for (int row = 0; row < h; row++) {
        int off = ((y + row) * m_fbWidth + x) * 4;
        if (!readExact(reinterpret_cast<char *>(m_framebuffer.data() + off), bytesPerRow))
            return false;
    }
    return true;
}

bool RfbConnection::handleCopyRectEncoding(int x, int y, int w, int h)
{
    char srcBuf[4];
    if (!readExact(srcBuf, 4)) return false;
    int srcX = (uint8_t)srcBuf[0] << 8 | (uint8_t)srcBuf[1];
    int srcY = (uint8_t)srcBuf[2] << 8 | (uint8_t)srcBuf[3];

    // Validate source rect bounds
    if (!validateRect(srcX, srcY, w, h)) {
        qWarning() << "[VNC] CopyRect source rect out of bounds:" << srcX << srcY << w << h;
        return false;
    }

    std::vector<uint8_t> temp(w * h * 4);
    for (int row = 0; row < h; row++) {
        memcpy(temp.data() + row * w * 4,
               m_framebuffer.data() + ((srcY + row) * m_fbWidth + srcX) * 4, w * 4);
    }
    for (int row = 0; row < h; row++) {
        memcpy(m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4,
               temp.data() + row * w * 4, w * 4);
    }
    return true;
}

bool RfbConnection::handleHextileEncoding(int rx, int ry, int rw, int rh)
{
    uint32_t bg = 0, fg = 0;

    for (int ty = ry; ty < ry + rh; ty += 16) {
        int th = std::min(16, ry + rh - ty);
        for (int tx = rx; tx < rx + rw; tx += 16) {
            int tw = std::min(16, rx + rw - tx);

            uint8_t subenc;
            if (!readExact((char *)&subenc, 1)) return false;

            if (subenc & RFB_HEXTILE_RAW) {
                // Raw tile
                for (int row = 0; row < th; row++) {
                    int off = ((ty + row) * m_fbWidth + tx) * 4;
                    if (!readExact(reinterpret_cast<char *>(m_framebuffer.data() + off), tw * 4))
                        return false;
                }
                continue;
            }

            if (subenc & RFB_HEXTILE_BG_SPECIFIED) {
                if (!readExact((char *)&bg, 4)) return false;
            }
            // Fill tile with background
            for (int row = 0; row < th; row++) {
                uint32_t *line = reinterpret_cast<uint32_t *>(
                    m_framebuffer.data() + ((ty + row) * m_fbWidth + tx) * 4);
                for (int col = 0; col < tw; col++)
                    line[col] = bg;
            }

            if (subenc & RFB_HEXTILE_FG_SPECIFIED) {
                if (!readExact((char *)&fg, 4)) return false;
            }

            if (!(subenc & RFB_HEXTILE_ANY_SUBRECTS))
                continue;

            uint8_t numSubrects;
            if (!readExact((char *)&numSubrects, 1)) return false;

            bool coloured = subenc & RFB_HEXTILE_SUBRECTS_COLOURED;

            for (int s = 0; s < numSubrects; s++) {
                uint32_t color = fg;
                if (coloured) {
                    if (!readExact((char *)&color, 4)) return false;
                }
                uint8_t xy, wh;
                if (!readExact((char *)&xy, 1)) return false;
                if (!readExact((char *)&wh, 1)) return false;
                int sx = tx + (xy >> 4);
                int sy = ty + (xy & 0x0F);
                int sw = (wh >> 4) + 1;
                int sh = (wh & 0x0F) + 1;

                for (int row = 0; row < sh; row++) {
                    uint32_t *line = reinterpret_cast<uint32_t *>(
                        m_framebuffer.data() + ((sy + row) * m_fbWidth + sx) * 4);
                    for (int col = 0; col < sw; col++)
                        line[col] = color;
                }
            }
        }
    }
    return true;
}

bool RfbConnection::handleZlibEncoding(int x, int y, int w, int h)
{
    // Read 4-byte compressed length
    char hdr[4];
    if (!readExact(hdr, 4)) return false;
    uint32_t compressedLen = (uint8_t)hdr[0] << 24 | (uint8_t)hdr[1] << 16 |
                             (uint8_t)hdr[2] << 8  | (uint8_t)hdr[3];

    // Check for integer overflow
    if (w <= 0 || h <= 0 || static_cast<long long>(w) * h * 4 > INT_MAX) return false;

    int rawLen = w * h * 4;
    // Sanity check on compressed length
    if (compressedLen > 64 * 1024 * 1024) return false;

    std::vector<char> compressed(compressedLen);
    std::vector<uint8_t> rawBuf(rawLen);

    if (!readExact(compressed.data(), compressedLen)) return false;

    // Initialize zlib stream once
    if (!m_decompStreamInited) {
        m_decompStream.zalloc = Z_NULL;
        m_decompStream.zfree = Z_NULL;
        m_decompStream.opaque = Z_NULL;
        if (inflateInit(&m_decompStream) != Z_OK) return false;
        m_decompStreamInited = true;
    }

    m_decompStream.next_in = reinterpret_cast<Bytef *>(compressed.data());
    m_decompStream.avail_in = compressedLen;
    m_decompStream.next_out = rawBuf.data();
    m_decompStream.avail_out = rawLen;

    int ret = inflate(&m_decompStream, Z_SYNC_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) return false;

    // Copy decompressed pixels into framebuffer
    for (int row = 0; row < h; row++) {
        memcpy(m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4,
               rawBuf.data() + row * w * 4, w * 4);
    }
    return true;
}

// --- Tight encoding ---

int RfbConnection::tightReadCompactLen()
{
    uint8_t b;
    int len;
    if (!readExact((char *)&b, 1)) return -1;
    len = b & 0x7F;
    if (!(b & 0x80)) return len;

    if (!readExact((char *)&b, 1)) return -1;
    len |= (b & 0x7F) << 7;
    if (!(b & 0x80)) return len;

    if (!readExact((char *)&b, 1)) return -1;
    len |= (b & 0xFF) << 14;
    // Sanity check: cap at 64MB to prevent excessive memory allocation
    if (len > 64 * 1024 * 1024) return -1;
    return len;
}

bool RfbConnection::tightDecompress(int streamId, const char *compressed, int compressedLen,
                                     char *out, int outLen)
{
    z_stream &zs = m_tightStreams[streamId];
    if (!m_tightStreamActive[streamId]) {
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        if (inflateInit(&zs) != Z_OK) return false;
        m_tightStreamActive[streamId] = true;
    }

    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed));
    zs.avail_in = compressedLen;
    zs.next_out = reinterpret_cast<Bytef *>(out);
    zs.avail_out = outLen;

    int ret = inflate(&zs, Z_SYNC_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) return false;

    int produced = outLen - zs.avail_out;
    if (produced != outLen) {
        qDebug() << "[TIGHT] decompress incomplete: expected" << outLen << "got" << produced;
        return false;
    }
    return true;
}

bool RfbConnection::tightFilterCopy(const char *src, int srcLen, int x, int y, int w, int h)
{
    if (m_tightCutZeros) {
        // 3 bytes per pixel (RGB), expand to 4 bytes (BGRX)
        int rowSrcLen = w * 3;
        for (int row = 0; row < h; row++) {
            int srcOff = row * rowSrcLen;
            if (srcOff + rowSrcLen > srcLen) return false;
            uint32_t *line = reinterpret_cast<uint32_t *>(
                m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4);
            const uint8_t *p = reinterpret_cast<const uint8_t *>(src + srcOff);
            for (int col = 0; col < w; col++) {
                line[col] = (p[0] << 16) | (p[1] << 8) | p[2];
                p += 3;
            }
        }
    } else {
        // 4 bytes per pixel, direct copy
        for (int row = 0; row < h; row++) {
            int srcOff = row * w * 4;
            int dstOff = ((y + row) * m_fbWidth + x) * 4;
            if (srcOff + w * 4 > srcLen) return false;
            memcpy(m_framebuffer.data() + dstOff, src + srcOff, w * 4);
        }
    }
    return true;
}

bool RfbConnection::tightFilterPalette(const char *src, int srcLen, int x, int y, int w, int h, int paletteSize)
{
    // Palette was already read from the wire into m_tightPalette.
    // The decompressed data (src) contains only pixel indices.
    const char *indices = src;
    int indicesLen = srcLen;

    if (paletteSize == 2) {
        // 1 bit per pixel, MSB first
        int totalPixels = w * h;
        if (indicesLen < (totalPixels + 7) / 8) return false;

        int idx = 0;
        for (int row = 0; row < h; row++) {
            uint32_t *line = reinterpret_cast<uint32_t *>(
                m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4);
            for (int col = 0; col < w; col++) {
                int byteIdx = idx >> 3;
                int bitIdx = 7 - (idx & 7);
                int colorIdx = (indices[byteIdx] >> bitIdx) & 1;
                uint32_t color;
                memcpy(&color, m_tightPalette.data() + colorIdx * 4, 4);
                line[col] = color;
                idx++;
            }
        }
    } else {
        // 1 byte per pixel index
        if (indicesLen < w * h) return false;
        for (int row = 0; row < h; row++) {
            uint32_t *line = reinterpret_cast<uint32_t *>(
                m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4);
            for (int col = 0; col < w; col++) {
                int colorIdx = static_cast<uint8_t>(indices[row * w + col]);
                uint32_t color;
                memcpy(&color, m_tightPalette.data() + colorIdx * 4, 4);
                line[col] = color;
            }
        }
    }
    return true;
}

bool RfbConnection::tightFilterGradient(const char *src, int srcLen, int x, int y, int w, int h)
{
    // Gradient filter: delta prediction using previous row
    // Source data is 3 bytes per pixel (RGB), output is 4 bytes per pixel (BGRX)
    const int rowLen = w * 3;
    if (m_tightPrevRow.size() < static_cast<size_t>(rowLen))
        m_tightPrevRow.resize(rowLen, 0);
    memset(m_tightPrevRow.data(), 0, rowLen);

    for (int row = 0; row < h; row++) {
        if ((row + 1) * rowLen > srcLen) return false;

        uint32_t *fbLine = reinterpret_cast<uint32_t *>(
            m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4);

        int prevRow[3] = {0, 0, 0};
        for (int col = 0; col < w; col++) {
            const uint8_t *p = reinterpret_cast<const uint8_t *>(src + row * rowLen + col * 3);

            // Estimate from left and above
            int est[3];
            for (int c = 0; c < 3; c++) {
                int left = (col > 0) ? prevRow[c] : 0;
                int above = m_tightPrevRow[col * 3 + c];
                int aboveLeft = (col > 0) ? m_tightPrevRow[(col - 1) * 3 + c] : 0;
                est[c] = left + above - aboveLeft;
            }

            uint8_t rgb[3];
            for (int c = 0; c < 3; c++) {
                int val = p[c] + est[c];
                // Clamp
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                rgb[c] = static_cast<uint8_t>(val);
                prevRow[c] = val;
            }

            // Store as 0xRRGGBB for QImage::Format_RGB32
            fbLine[col] = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];

            for (int c = 0; c < 3; c++)
                m_tightPrevRow[col * 3 + c] = prevRow[c];
        }
    }
    return true;
}

bool RfbConnection::handleTightEncoding(int x, int y, int w, int h)
{
    uint8_t compCtl;
    if (!readExact((char *)&compCtl, 1)) return false;

    // Flush zlib streams if the server tells us to (bits 0-3 of compCtl)
    for (int stream_id = 0; stream_id < 4; stream_id++) {
        if ((compCtl & 1) && m_tightStreamActive[stream_id]) {
            inflateEnd(&m_tightStreams[stream_id]);
            m_tightStreamActive[stream_id] = false;
        }
        compCtl >>= 1;
    }
    // After shifting 4 times, compCtl now holds the subencoding (was bits 4-7)

    // Fill compression
    if (compCtl == RFB_TIGHT_FILL) {
        uint32_t color = 0;
        if (m_tightCutZeros) {
            uint8_t rgb[3];
            if (!readExact((char *)rgb, 3)) { qDebug() << "[TIGHT] Fill read3 failed"; return false; }
            color = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
        } else {
            if (!readExact((char *)&color, 4)) { qDebug() << "[TIGHT] Fill read4 failed"; return false; }
        }
        qDebug() << "[TIGHT] Fill color=0x" << hex << color;
        for (int row = 0; row < h; row++) {
            uint32_t *line = reinterpret_cast<uint32_t *>(
                m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4);
            for (int col = 0; col < w; col++)
                line[col] = color;
        }
        return true;
    }

    // JPEG compression
    if (compCtl == RFB_TIGHT_JPEG) {
        int len = tightReadCompactLen();
        if (len < 0) { qDebug() << "[TIGHT] JPEG compactLen failed"; return false; }
        qDebug() << "[TIGHT] JPEG len=" << len;
        QByteArray jpegData(len, 0);
        if (!readExact(jpegData.data(), len)) { qDebug() << "[TIGHT] JPEG read failed"; return false; }

        QImage img;
        if (!img.loadFromData(jpegData, "JPEG")) { qDebug() << "[TIGHT] JPEG decode failed"; return false; }

        QImage converted = img.convertToFormat(QImage::Format_RGB32);
        for (int row = 0; row < std::min(h, converted.height()); row++) {
            const uint32_t *srcLine = reinterpret_cast<const uint32_t *>(converted.constScanLine(row));
            uint32_t *dstLine = reinterpret_cast<uint32_t *>(
                m_framebuffer.data() + ((y + row) * m_fbWidth + x) * 4);
            int copyW = std::min(w, converted.width());
            memcpy(dstLine, srcLine, copyW * 4);
        }
        return true;
    }

    // Basic compression (zlib or uncompressed)
    int filterId = RFB_TIGHT_FILTER_COPY;
    if (compCtl & RFB_TIGHT_EXPLICIT_FILTER) {
        uint8_t fb;
        if (!readExact((char *)&fb, 1)) { qDebug() << "[TIGHT] filter read failed"; return false; }
        filterId = fb;
    }
    int streamId = compCtl & RFB_TIGHT_MAX_SERVER_STREAM;
    qDebug() << "[TIGHT] filter=" << filterId << "streamId=" << streamId;

    // Read palette for palette filter
    int bytesPerPixel = m_tightCutZeros ? 3 : 4;
    int bitsPerPixel = m_tightCutZeros ? 24 : 32;
    if (filterId == RFB_TIGHT_FILTER_PALETTE) {
        uint8_t numColors;
        if (!readExact((char *)&numColors, 1)) { qDebug() << "[TIGHT] palette count read failed"; return false; }
        int paletteSize = numColors + 1;
        m_tightPalette.resize(paletteSize * 4, 0);
        int paletteBytes = paletteSize * bytesPerPixel;
        std::vector<char> palBuf(paletteBytes);
        if (!readExact(palBuf.data(), paletteBytes))
        { qDebug() << "[TIGHT] palette data read failed"; return false; }
        for (int i = 0; i < paletteSize; i++) {
            if (m_tightCutZeros) {
                uint8_t r = static_cast<uint8_t>(palBuf[i * 3]);
                uint8_t g = static_cast<uint8_t>(palBuf[i * 3 + 1]);
                uint8_t b = static_cast<uint8_t>(palBuf[i * 3 + 2]);
                uint32_t color = (r << 16) | (g << 8) | b;
                memcpy(m_tightPalette.data() + i * 4, &color, 4);
            } else {
                memcpy(m_tightPalette.data() + i * 4, palBuf.data() + i * 4, 4);
            }
        }
        bitsPerPixel = (paletteSize == 2) ? 1 : 8;
    } else if (filterId == RFB_TIGHT_FILTER_GRADIENT) {
        bitsPerPixel = 24;
    }

    int rowSize = (w * bitsPerPixel + 7) / 8;
    int totalDataLen = rowSize * h;
    qDebug() << "[TIGHT] bitsPerPixel=" << bitsPerPixel << "rowSize=" << rowSize << "totalDataLen=" << totalDataLen;

    // For small data, it's sent uncompressed
    if (totalDataLen < RFB_TIGHT_MIN_TO_COMPRESS) {
        std::vector<char> buf(totalDataLen);
        if (!readExact(buf.data(), totalDataLen)) { qDebug() << "[TIGHT] small data read failed"; return false; }

        switch (filterId) {
        case RFB_TIGHT_FILTER_COPY:
            return tightFilterCopy(buf.data(), totalDataLen, x, y, w, h);
        case RFB_TIGHT_FILTER_PALETTE:
            return tightFilterPalette(buf.data(), totalDataLen, x, y, w, h,
                                      m_tightPalette.size() / 4);
        case RFB_TIGHT_FILTER_GRADIENT:
            return tightFilterGradient(buf.data(), totalDataLen, x, y, w, h);
        default:
            return false;
        }
    }

    // Read compressed data
    int compressedLen = tightReadCompactLen();
    if (compressedLen < 0) { qDebug() << "[TIGHT] compactLen failed"; return false; }
    qDebug() << "[TIGHT] compressedLen=" << compressedLen;
    std::vector<char> compressed(compressedLen);
    if (!readExact(compressed.data(), compressedLen)) { qDebug() << "[TIGHT] compressed read failed"; return false; }

    // Decompress
    std::vector<char> decompressed(totalDataLen);
    if (!tightDecompress(streamId, compressed.data(), compressedLen,
                         decompressed.data(), totalDataLen))
    { qDebug() << "[TIGHT] decompress failed"; return false; }

    qDebug() << "[TIGHT] decompress OK, applying filter" << filterId;
    switch (filterId) {
    case RFB_TIGHT_FILTER_COPY:
        return tightFilterCopy(decompressed.data(), totalDataLen, x, y, w, h);
    case RFB_TIGHT_FILTER_PALETTE:
        return tightFilterPalette(decompressed.data(), totalDataLen, x, y, w, h,
                                  m_tightPalette.size() / 4);
    case RFB_TIGHT_FILTER_GRADIENT:
        return tightFilterGradient(decompressed.data(), totalDataLen, x, y, w, h);
    default:
        return false;
    }
}

bool RfbConnection::handleDesktopSizeEncoding(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || static_cast<long long>(w) * h * 4 > INT_MAX) return false;
    m_fbWidth = w;
    m_fbHeight = h;
    m_framebuffer.assign(w * h * 4, 0);
    m_pendingUpdateRequest = false;
    requestFramebufferUpdate(false);
    return true;
}

bool RfbConnection::handleCursorEncoding(int x, int y, int w, int h)
{
    int stride = ((w + 7) / 8) * h;
    int pixelBytes = w * h * 4;
    std::vector<char> skipBuf(stride + pixelBytes);
    if (!readExact(skipBuf.data(), stride + pixelBytes)) return false;
    return true;
}

// --- Simple message handlers ---

bool RfbConnection::handleSetColourMap()
{
    char header[5];
    if (!readExact(header, 5)) return false;
    uint16_t numColors = (uint8_t)header[3] << 8 | (uint8_t)header[4];
    for (uint16_t i = 0; i < numColors; i++) {
        char dummy[6];
        if (!readExact(dummy, 6)) return false;
    }
    return true;
}

bool RfbConnection::handleBell() { return true; }

bool RfbConnection::handleServerCutText()
{
    char header[7];
    if (!readExact(header, 7)) return false;
    uint32_t textLen = (uint8_t)header[3] << 24 | (uint8_t)header[4] << 16 |
                       (uint8_t)header[5] << 8  | (uint8_t)header[6];
    if (textLen > 1048576) textLen = 1048576;
    QByteArray text(textLen, 0);
    if (textLen > 0 && !readExact(text.data(), textLen)) return false;
    return true;
}

// --- Connection lifecycle ---

void RfbConnection::connectToHost(const QString &host, int port, const QString &password)
{
    m_host = host;
    m_port = port;
    m_password = password;
    m_running = true;
    m_pendingUpdateRequest = false;

    m_socket = new QTcpSocket(this);
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(10000)) {
        qWarning() << "[VNC] TCP connect failed:" << m_socket->errorString();
        emit errorOccurred(tr("Connection failed: %1").arg(m_socket->errorString()));
        m_socket->deleteLater();
        m_socket = nullptr;
        m_running = false;
        return;
    }

    if (!negotiateVersion()) {
        qWarning() << "[VNC] Version negotiation failed";
        emit errorOccurred(tr("Protocol version negotiation failed"));
        m_socket->close(); m_socket->deleteLater(); m_socket = nullptr;
        m_running = false; return;
    }
    qWarning() << "[VNC] Version OK, server" << m_serverMajor << m_serverMinor;
    if (!negotiateSecurity()) {
        qWarning() << "[VNC] Security negotiation failed";
        emit errorOccurred(tr("Authentication failed"));
        m_socket->close(); m_socket->deleteLater(); m_socket = nullptr;
        m_running = false; return;
    }
    if (!clientInit()) {
        emit errorOccurred(tr("Client init failed"));
        m_socket->close(); m_socket->deleteLater(); m_socket = nullptr;
        m_running = false; return;
    }
    if (!serverInit()) {
        emit errorOccurred(tr("Server init failed"));
        m_socket->close(); m_socket->deleteLater(); m_socket = nullptr;
        m_running = false; return;
    }
    if (!setEncodings()) {
        qWarning() << "[VNC] Set encodings failed";
        emit errorOccurred(tr("Set encodings failed"));
        m_socket->close(); m_socket->deleteLater(); m_socket = nullptr;
        m_running = false; return;
    }

    requestFramebufferUpdate(false);
    qWarning() << "[VNC] Connected, waiting for first frame...";
    emit connected();

    // Main loop
    while (m_running) {
        if (!m_socket->waitForReadyRead(33)) {
            if (m_socket->state() != QAbstractSocket::ConnectedState) {
                emit errorOccurred(tr("Connection lost"));
                break;
            }
            requestFramebufferUpdate(true);
            continue;
        }

        bool gotFrame = false;
        while (m_socket->bytesAvailable() > 0 && m_running) {
            if (!handleServerMessage()) {
                if (m_running) emit errorOccurred(tr("Server communication error"));
                goto done;
            }
            gotFrame = true;
        }

        if (gotFrame) {
            m_pendingUpdateRequest = false;
            requestFramebufferUpdate(true);
        }
    }

done:
    cleanupZlib();
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
    m_running = false;
    emit disconnected();
}

void RfbConnection::disconnect()
{
    m_running = false;
}
