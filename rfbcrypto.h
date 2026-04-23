#ifndef RFBCRYPTO_H
#define RFBCRYPTO_H

#include <cstdint>
#include <QByteArray>

// VNC Auth: encrypt a 16-byte challenge with the given password using DES
QByteArray vncEncryptChallenge(const QByteArray &challenge, const QByteArray &password);

#endif // RFBCRYPTO_H
