#include "artcade-crypto.h"

#include <cstring>
#include <random>

#if ARTCADE_HAS_CRYPTO
#include "monocypher.h"
#include "artcade-asset-key.h"
#endif

namespace ArtCade {

bool artcadeArchiveIsEncrypted(const std::vector<uint8_t>& data) {
    if (data.size() < static_cast<size_t>(kArtcadeCryptoMagicLen)) return false;
    return std::memcmp(data.data(), kArtcadeCryptoMagic, kArtcadeCryptoMagicLen) == 0;
}

bool artcadeDecryptArchive(std::vector<uint8_t>& data) {
    if (!artcadeArchiveIsEncrypted(data)) return true;

#if ARTCADE_HAS_CRYPTO
    if (data.size() < static_cast<size_t>(kArtcadeCryptoHeaderLen)) return false;

    const uint8_t* p = data.data();
    size_t off = kArtcadeCryptoMagicLen;
    const uint8_t version = p[off++];
    const uint8_t flags   = p[off++];
    if (version != kArtcadeCryptoVersion) return false;
    if ((flags & kArtcadeCryptoFlagEncrypted) == 0) return false;

    const uint8_t* nonce = p + off; off += kArtcadeCryptoNonceLen;
    const uint8_t* mac   = p + off; off += kArtcadeCryptoMacLen;

    const size_t cipherLen = data.size() - off;
    const uint8_t* cipher = p + off;

    std::vector<uint8_t> plain(cipherLen);
    const int rc = crypto_aead_unlock(
        plain.data(), mac, kArtcadeAssetKey, nonce,
        nullptr, 0,
        cipher, cipherLen);
    if (rc != 0) return false;

    data.swap(plain);
    return true;
#else
    return false;
#endif
}

bool artcadeEncryptArchive(const std::vector<uint8_t>& plaintextZip,
                           std::vector<uint8_t>& outContainer) {
#if ARTCADE_HAS_CRYPTO
    uint8_t nonce[kArtcadeCryptoNonceLen];
    {
        std::random_device rd;
        for (int i = 0; i < kArtcadeCryptoNonceLen; ++i)
            nonce[i] = static_cast<uint8_t>(rd());
    }

    std::vector<uint8_t> cipher(plaintextZip.size());
    uint8_t mac[kArtcadeCryptoMacLen];
    crypto_aead_lock(
        cipher.data(), mac, kArtcadeAssetKey, nonce,
        nullptr, 0,
        plaintextZip.data(), plaintextZip.size());

    outContainer.clear();
    outContainer.reserve(static_cast<size_t>(kArtcadeCryptoHeaderLen) + cipher.size());
    outContainer.insert(outContainer.end(),
                        kArtcadeCryptoMagic,
                        kArtcadeCryptoMagic + kArtcadeCryptoMagicLen);
    outContainer.push_back(kArtcadeCryptoVersion);
    outContainer.push_back(kArtcadeCryptoFlagEncrypted);
    outContainer.insert(outContainer.end(), nonce, nonce + kArtcadeCryptoNonceLen);
    outContainer.insert(outContainer.end(), mac, mac + kArtcadeCryptoMacLen);
    outContainer.insert(outContainer.end(), cipher.begin(), cipher.end());
    return true;
#else
    (void)plaintextZip;
    (void)outContainer;
    return false;
#endif
}

} // namespace ArtCade
