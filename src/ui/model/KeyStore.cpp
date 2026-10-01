#include "KeyStore.h"

#include <windows.h>
#include <wincrypt.h>

namespace vgo {

namespace {

QByteArray blobToBytes(const DATA_BLOB& blob) {
    return QByteArray(reinterpret_cast<const char*>(blob.pbData),
                      static_cast<int>(blob.cbData));
}

}  // namespace

QByteArray KeyStore::protect(const QByteArray& plain) {
    if (plain.isEmpty()) return {};
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{0, nullptr};
    if (!CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    const QByteArray cipher = blobToBytes(out);
    if (out.pbData) LocalFree(out.pbData);
    return cipher;
}

QByteArray KeyStore::unprotect(const QByteArray& cipher) {
    if (cipher.isEmpty()) return {};
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData()));
    in.cbData = static_cast<DWORD>(cipher.size());
    DATA_BLOB out{0, nullptr};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
    }
    const QByteArray plain = blobToBytes(out);
    if (out.pbData) LocalFree(out.pbData);
    return plain;
}

QString KeyStore::mask(const QString& key) {
    if (key.isEmpty()) return QStringLiteral("未设置");
    if (key.size() <= 4) return QStringLiteral("••••");
    return QStringLiteral("••••") + key.right(4);
}

}  // namespace vgo
