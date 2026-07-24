// ---------------------------------------------------------------------------
// artcade-crypto — .artcade archive decryption (XChaCha20-Poly1305)
// ---------------------------------------------------------------------------
//
// Shipped .artcade archives are wrapped in an authenticated-encryption
// container around the plaintext ZIP. The container layout is:
//
//   offset  size  field
//   0       8     magic   "ARTCADE1"
//   8       1     version (=1)
//   9       1     flags   (bit0 = encrypted payload)
//   10      24    nonce   (XChaCha20)
//   34      16    mac     (Poly1305 tag)
//   50      N     ciphertext (same length as the plaintext ZIP)
//
// A plaintext ZIP (begins with "PK\x03\x04") carries no such header and is
// passed through untouched, so dev/unencrypted archives still load.
//
// SECURITY: the key is embedded in the client (and the WASM, which is trivially
// inspectable). This deters casual asset ripping; it is not DRM. See
// docs/ASSET_ENCRYPTION.md.

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

/// True if `data` begins with the encryption container magic.
bool artcadeArchiveIsEncrypted(const std::vector<uint8_t>& data);

/// Decrypt an .artcade archive in place.
///
/// - Plaintext ZIP (no container magic): returns true, leaves `data` untouched.
/// - Encrypted container: verifies the AEAD tag, replaces `data` with the
///   plaintext ZIP, returns true. Returns false on auth failure, malformed
///   container, unsupported version, or when the build has no crypto support.
bool artcadeDecryptArchive(std::vector<uint8_t>& data);

} // namespace ArtCade
