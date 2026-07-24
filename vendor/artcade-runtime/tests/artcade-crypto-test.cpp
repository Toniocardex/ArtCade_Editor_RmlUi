// ---------------------------------------------------------------------------
// artcade-crypto-test — XChaCha20-Poly1305 .artcade container round-trip
//
// Hermetic: builds an encrypted container with Monocypher using the same
// compiled-in key the runtime decrypts with, then exercises
// artcadeDecryptArchive() for the valid / tampered / plaintext-passthrough
// cases. Cross-implementation (PyNaCl pack → Monocypher unpack) compatibility
// is covered separately by tools/test_pack_artcade.py + the build pipeline.
// ---------------------------------------------------------------------------

#include "artcade-crypto.h"
#include "monocypher.h"
#include "artcade-asset-key.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ArtCade;

namespace {

// Wrap a plaintext buffer in the .artcade encryption container the way the
// packer does (magic | version | flags | nonce | mac | ciphertext).
std::vector<uint8_t> makeContainer(const std::vector<uint8_t>& plain) {
    uint8_t nonce[kArtcadeCryptoNonceLen];
    for (int i = 0; i < kArtcadeCryptoNonceLen; i++) nonce[i] = static_cast<uint8_t>(0x40 + i);

    std::vector<uint8_t> cipher(plain.size());
    uint8_t mac[kArtcadeCryptoMacLen];
    crypto_aead_lock(cipher.data(), mac, kArtcadeAssetKey, nonce,
                     nullptr, 0, plain.data(), plain.size());

    std::vector<uint8_t> out;
    out.insert(out.end(), kArtcadeCryptoMagic, kArtcadeCryptoMagic + kArtcadeCryptoMagicLen);
    out.push_back(kArtcadeCryptoVersion);
    out.push_back(kArtcadeCryptoFlagEncrypted);
    out.insert(out.end(), nonce, nonce + kArtcadeCryptoNonceLen);
    out.insert(out.end(), mac, mac + kArtcadeCryptoMacLen);
    out.insert(out.end(), cipher.begin(), cipher.end());
    return out;
}

} // namespace

int main() {
    // Plausible plaintext payload (mimics a ZIP header so callers downstream
    // recognise it; the crypto layer itself does not parse it).
    std::vector<uint8_t> plain = { 'P', 'K', 0x03, 0x04 };
    for (int i = 0; i < 200; i++) plain.push_back(static_cast<uint8_t>(i * 7 + 3));

    // 1. Valid container decrypts in place to the original plaintext.
    {
        std::vector<uint8_t> data = makeContainer(plain);
        assert(artcadeArchiveIsEncrypted(data));
        const bool ok = artcadeDecryptArchive(data);
        assert(ok);
        assert(data == plain);
    }

    // 2. Tampered ciphertext fails authentication.
    {
        std::vector<uint8_t> data = makeContainer(plain);
        data.back() ^= 0x01;
        assert(!artcadeDecryptArchive(data));
    }

    // 3. Tampered MAC fails authentication.
    {
        std::vector<uint8_t> data = makeContainer(plain);
        data[kArtcadeCryptoHeaderLen - 1] ^= 0x01; // last MAC byte
        assert(!artcadeDecryptArchive(data));
    }

    // 4. Plaintext ZIP (no container magic) passes through untouched.
    {
        std::vector<uint8_t> data = plain;
        assert(!artcadeArchiveIsEncrypted(data));
        const bool ok = artcadeDecryptArchive(data);
        assert(ok);
        assert(data == plain);
    }

    // 5. Truncated container is rejected, not crashed.
    {
        std::vector<uint8_t> data = makeContainer(plain);
        data.resize(kArtcadeCryptoHeaderLen - 5);
        assert(!artcadeDecryptArchive(data));
    }

    std::printf("artcade-crypto-test: OK\n");
    return 0;
}
