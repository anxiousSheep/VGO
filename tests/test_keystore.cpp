#include "test_framework.h"

#include "ui/model/KeyStore.h"

using namespace vgo;

TEST(keystore_roundtrip) {
    const QByteArray secret = "sk-test-1234567890";
    const QByteArray enc = KeyStore::protect(secret);
    CHECK(!enc.isEmpty());
    CHECK(KeyStore::unprotect(enc) == secret);
}

TEST(keystore_empty_protect_is_empty) {
    CHECK(KeyStore::protect(QByteArray()).isEmpty());
}

TEST(keystore_mask) {
    CHECK(KeyStore::mask(QStringLiteral("sk-abcdef1234")) == QStringLiteral("••••1234"));
    CHECK(KeyStore::mask(QString()) == QStringLiteral("未设置"));
}
