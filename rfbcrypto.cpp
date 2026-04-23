#include "rfbcrypto.h"
extern "C" {
#include "d3des.h"
}
#include <cstring>

QByteArray vncEncryptChallenge(const QByteArray &challenge, const QByteArray &password)
{
    // Pad or truncate password to 8 bytes
    // d3des handles bit reversal internally via its modified bytebit array,
    // so we pass the key bytes directly (no manual reverseBits needed).
    unsigned char key[8] = {};
    for (int i = 0; i < qMin(password.size(), 8); i++)
        key[i] = static_cast<unsigned char>(password[i]);

    rfbDesKey(key, EN0);

    // Encrypt 16-byte challenge in two 8-byte blocks (in-place)
    QByteArray result = challenge;
    rfbDes((unsigned char *)result.data(), (unsigned char *)result.data());
    rfbDes((unsigned char *)result.data() + 8, (unsigned char *)result.data() + 8);

    return result;
}
