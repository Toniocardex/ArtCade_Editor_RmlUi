#include "artcade-crypto.h"

#include <cstring>

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
    // Plaintext ZIP (or anything without our magic) → pass through untouched.
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
        nullptr, 0,          // no associated data
        cipher, cipherLen);
    if (rc != 0) return false; // forged / wrong key / corrupted

    data.swap(plain);
    return true;
#else
    // Encrypted archive but this build has no crypto support.
    return false;
#endif
}

} // namespace ArtCade
