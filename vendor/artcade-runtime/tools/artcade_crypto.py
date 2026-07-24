#!/usr/bin/env python3
# =============================================================================
# artcade_crypto — shared XChaCha20-Poly1305 container encode/decode (Python)
#
# Matches the C++ container in
# runtime-cpp/src/modules/asset-system/include/artcade-crypto.h:
#   magic[8] "ARTCADE1" | version u8=1 | flags u8 | nonce[24] | mac[16] | ct[N]
#
# Used by the packer (whole-archive wrap) and the delta-update tools
# (per-object wrap). PyNaCl's combined-mode tag is appended to the ciphertext;
# we split the last 16 bytes into the MAC field so Monocypher (separate MAC)
# can verify it.
# =============================================================================

import os

MAGIC = b"ARTCADE1"
VERSION = 1
FLAG_ENCRYPTED = 0x01
NONCE_LEN = 24
MAC_LEN = 16
HEADER_LEN = len(MAGIC) + 1 + 1 + NONCE_LEN + MAC_LEN  # 50


def is_container(data: bytes) -> bool:
    return len(data) >= len(MAGIC) and data[: len(MAGIC)] == MAGIC


def encrypt(plaintext: bytes, key: bytes) -> bytes:
    """Wrap plaintext in the .artcade encryption container."""
    from nacl import bindings as nb
    nonce = os.urandom(NONCE_LEN)
    combined = nb.crypto_aead_xchacha20poly1305_ietf_encrypt(plaintext, b"", nonce, key)
    ciphertext, mac = combined[:-MAC_LEN], combined[-MAC_LEN:]
    return MAGIC + bytes([VERSION, FLAG_ENCRYPTED]) + nonce + mac + ciphertext


def decrypt(container: bytes, key: bytes) -> bytes:
    """Verify and unwrap a container. Raises on bad magic/version or auth fail."""
    from nacl import bindings as nb
    if not is_container(container):
        raise ValueError("not an .artcade encryption container")
    if len(container) < HEADER_LEN:
        raise ValueError("truncated container")
    version = container[len(MAGIC)]
    flags = container[len(MAGIC) + 1]
    if version != VERSION:
        raise ValueError(f"unsupported container version {version}")
    if not (flags & FLAG_ENCRYPTED):
        raise ValueError("container not marked encrypted")
    nonce = container[10:34]
    mac = container[34:50]
    ciphertext = container[50:]
    return nb.crypto_aead_xchacha20poly1305_ietf_decrypt(ciphertext + mac, b"", nonce, key)
