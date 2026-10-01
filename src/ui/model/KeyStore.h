#pragma once

#include <QByteArray>
#include <QString>

namespace vgo {

// Encrypts secrets at rest using Windows DPAPI (CryptProtectData /
// CryptUnprotectData, current-user scope). The API key is never written to
// disk in plaintext. All operations return an empty QByteArray on failure so
// callers can degrade gracefully.
class KeyStore {
public:
    // Returns empty on failure.
    static QByteArray protect(const QByteArray& plain);
    // Returns empty on failure (e.g. wrong machine/user).
    static QByteArray unprotect(const QByteArray& cipher);

    // "••••1234" for the tail, or "未设置" when empty.
    static QString mask(const QString& key);
};

}  // namespace vgo
