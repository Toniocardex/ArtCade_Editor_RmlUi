#!/usr/bin/env python3
# =============================================================================
# artcade_keytool — shared asset-encryption key resolution
#
# Both the packer (pack-artcade.py) and the C++ build (via
# emit-asset-key-header.py) resolve the 32-byte XChaCha20-Poly1305 key through
# THIS module so pack and unpack can never disagree.
#
# Resolution order (first hit wins):
#   1. env  ARTCADE_ASSET_KEY      — 64 hex chars (production / CI secret)
#   2. file runtime-cpp/secrets/artcade_key.hex  — 64 hex chars (local secret)
#   3. built-in DEV key            — INSECURE, dev-only; emits a warning
#
# SECURITY NOTE: the key is embedded in the shipped client (and the WASM, which
# is trivially inspectable). This is anti-casual-ripping obfuscation, NOT DRM.
# A determined attacker can extract the key and decrypt. See
# docs/ASSET_ENCRYPTION.md.
# =============================================================================

import os
import sys

KEY_BYTES = 32

# Recognisable dev key (bytes 0x00..0x1F). Never use for a real release.
DEV_KEY = bytes(range(KEY_BYTES))

ENV_VAR = "ARTCADE_ASSET_KEY"


def _repo_root() -> str:
    # tools/ lives under runtime-cpp/ ; secrets/ is a sibling of tools/.
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def secret_file_path() -> str:
    return os.path.join(_repo_root(), "secrets", "artcade_key.hex")


def _parse_hex_key(text: str, source: str) -> bytes:
    cleaned = text.strip().lower().replace("0x", "")
    try:
        raw = bytes.fromhex(cleaned)
    except ValueError as exc:
        raise ValueError(f"{source}: not valid hex ({exc})") from exc
    if len(raw) != KEY_BYTES:
        raise ValueError(
            f"{source}: key must be {KEY_BYTES} bytes ({KEY_BYTES * 2} hex chars), "
            f"got {len(raw)} bytes"
        )
    return raw


def resolve_key(*, warn: bool = True) -> tuple[bytes, bool]:
    """Return (key, is_dev). is_dev is True when falling back to the dev key."""
    env = os.environ.get(ENV_VAR)
    if env:
        return _parse_hex_key(env, f"${ENV_VAR}"), False

    path = secret_file_path()
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            return _parse_hex_key(f.read(), path), False

    if warn:
        print(
            f"[artcade-key] WARNING: no {ENV_VAR} and no {path} — "
            "using INSECURE built-in dev key. Do not ship this build.",
            file=sys.stderr,
        )
    return DEV_KEY, True
