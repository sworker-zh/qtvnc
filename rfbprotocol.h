#ifndef RFBPROTOCOL_H
#define RFBPROTOCOL_H

#include <cstdint>

// RFB protocol version
constexpr int RFB_VERSION_MAJOR = 3;
constexpr int RFB_VERSION_MINOR = 8;

// Security types
constexpr uint8_t RFB_SECURITY_INVALID = 0;
constexpr uint8_t RFB_SECURITY_NONE    = 1;
constexpr uint8_t RFB_SECURITY_VNCAUTH = 2;

// Security result
constexpr uint32_t RFB_SECURITY_OK     = 0;
constexpr uint32_t RFB_SECURITY_FAILED = 1;

// Client -> Server message types
constexpr uint8_t RFB_MSG_SET_PIXEL_FORMAT     = 0;
constexpr uint8_t RFB_MSG_SET_ENCODINGS         = 2;
constexpr uint8_t RFB_MSG_FRAMEBUFFER_UPDATE_REQ = 3;
constexpr uint8_t RFB_MSG_KEY_EVENT             = 4;
constexpr uint8_t RFB_MSG_POINTER_EVENT         = 5;
constexpr uint8_t RFB_MSG_CLIENT_CUT_TEXT       = 6;

// Server -> Client message types
constexpr uint8_t RFB_MSG_FRAMEBUFFER_UPDATE = 0;
constexpr uint8_t RFB_MSG_SET_COLOUR_MAP     = 1;
constexpr uint8_t RFB_MSG_BELL               = 2;
constexpr uint8_t RFB_MSG_SERVER_CUT_TEXT    = 3;

// Encodings
constexpr int32_t RFB_ENCODING_RAW          = 0;
constexpr int32_t RFB_ENCODING_COPYRECT     = 1;
constexpr int32_t RFB_ENCODING_HEXTILE      = 5;
constexpr int32_t RFB_ENCODING_ZLIB         = 6;
constexpr int32_t RFB_ENCODING_TIGHT        = 7;
constexpr int32_t RFB_ENCODING_ZRLE         = 16;
constexpr int32_t RFB_ENCODING_DESKTOPSIZE  = -223;
constexpr int32_t RFB_ENCODING_CURSOR       = -239;

// Hextile subencoding flags
constexpr uint8_t RFB_HEXTILE_RAW                 = 0x01;
constexpr uint8_t RFB_HEXTILE_BG_SPECIFIED        = 0x02;
constexpr uint8_t RFB_HEXTILE_FG_SPECIFIED        = 0x04;
constexpr uint8_t RFB_HEXTILE_ANY_SUBRECTS        = 0x08;
constexpr uint8_t RFB_HEXTILE_SUBRECTS_COLOURED   = 0x10;

// Tight compression control masks
constexpr uint8_t RFB_TIGHT_EXPLICIT_FILTER   = 0x04;
constexpr uint8_t RFB_TIGHT_FILL              = 0x08;
constexpr uint8_t RFB_TIGHT_JPEG              = 0x09;
constexpr uint8_t RFB_TIGHT_MAX_SERVER_STREAM = 0x03;

// Tight filter types
constexpr uint8_t RFB_TIGHT_FILTER_COPY     = 0x00;
constexpr uint8_t RFB_TIGHT_FILTER_PALETTE  = 0x01;
constexpr uint8_t RFB_TIGHT_FILTER_GRADIENT = 0x02;

// Tight threshold: data smaller than this is sent uncompressed
constexpr int RFB_TIGHT_MIN_TO_COMPRESS = 12;

// Pixel format structure (matches wire format, 16 bytes)
struct RfbPixelFormat {
    uint8_t  bitsPerPixel;
    uint8_t  depth;
    uint8_t  bigEndian;
    uint8_t  trueColour;
    uint16_t redMax;
    uint16_t greenMax;
    uint16_t blueMax;
    uint8_t  redShift;
    uint8_t  greenShift;
    uint8_t  blueShift;
    uint8_t  padding[3];
};

#endif // RFBPROTOCOL_H
