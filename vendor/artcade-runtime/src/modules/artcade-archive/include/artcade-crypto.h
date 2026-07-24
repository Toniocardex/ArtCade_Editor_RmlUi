// ---------------------------------------------------------------------------
// artcade-crypto — .artcade archive encryption / decryption (XChaCha20-Poly1305)
// ---------------------------------------------------------------------------
// Container layout (ADR-0019 / docs/ASSET_ENCRYPTION.md):
//   magic[8]="ARTCADE1" | version u8=1 | flags u8 | nonce[24] | mac[16] | ct[N]
#pragma once

#include <cstdint>
#include <vector>

namespace ArtCade {

inline constexpr int      kArtcadeCryptoMagicLen = 8;
inline constexpr char     kArtcadeCryptoMagic[kArtcadeCryptoMagicLen + 1] = "ARTCADE1";
inline constexpr uint8_t  kArtcadeCryptoVersion  = 1;
inline constexpr uint8_t  kArtcadeCryptoFlagEncrypted = 0x01;
inline constexpr int      kArtcadeCryptoNonceLen = 24;
inline constexpr int      kArtcadeCryptoMacLen   = 16;
inline constexpr int      kArtcadeCryptoHeaderLen =
    kArtcadeCryptoMagicLen + 1 /*version*/ + 1 /*flags*/ +
    kArtcadeCryptoNonceLen + kArtcadeCryptoMacLen; // = 50

bool artcadeArchiveIsEncrypted(const std::vector<uint8_t>& data);

/// Decrypt in place. Plaintext ZIP (no magic) passes through.
bool artcadeDecryptArchive(std::vector<uint8_t>& data);

/// Encrypt plaintext ZIP into ARTCADE1 container. Requires ARTCADE_HAS_CRYPTO.
bool artcadeEncryptArchive(const std::vector<uint8_t>& plaintextZip,
                           std::vector<uint8_t>& outContainer);

} // namespace ArtCade
