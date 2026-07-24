#!/usr/bin/env python3
# =============================================================================
# gen-asset-key.py — generate a fresh 32-byte asset-encryption key
#
# Writes runtime-cpp/secrets/artcade_key.hex (64 hex chars). This file is
# gitignored. For CI / production, set the ARTCADE_ASSET_KEY env var instead
# (same 64-hex format) so the secret never touches disk.
#
# Usage:
#   python tools/gen-asset-key.py            # write secrets/artcade_key.hex
#   python tools/gen-asset-key.py --print    # print to stdout, do not write
#   python tools/gen-asset-key.py --force    # overwrite an existing key file
# =============================================================================

import argparse
import os
import secrets
import sys

from artcade_keytool import KEY_BYTES, secret_file_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an asset-encryption key")
    parser.add_argument("--print", action="store_true", dest="to_stdout",
                        help="print the key to stdout instead of writing the file")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing key file")
    args = parser.parse_args()

    key_hex = secrets.token_bytes(KEY_BYTES).hex()

    if args.to_stdout:
        print(key_hex)
        return 0

    path = secret_file_path()
    if os.path.exists(path) and not args.force:
        print(f"[FAIL] {path} already exists. Use --force to overwrite.", file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(key_hex + "\n")
    print(f"[OK] wrote {path}")
    print("     (gitignored — back this up; losing it makes shipped games un-loadable)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
