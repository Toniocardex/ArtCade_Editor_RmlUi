#!/usr/bin/env python3
# =============================================================================
# update-client.py — apply a delta update from a release store
#
# Reference implementation of the client side of the delta-update protocol.
# A native launcher would embed this same logic; here it operates on local
# directories (the "store" can be a mounted dir, a synced folder, or a CDN
# mirror). It downloads only the objects whose content changed.
#
#   1. read channel-<name>.json -> latest version + manifest path
#   2. read the new manifest; compare to the locally-installed manifest
#   3. fetch only objects whose sha256 differs/missing, decrypt + verify
#   4. apply atomically (all-or-nothing — a bad object aborts before any write)
#   5. delete files no longer in the manifest; record the new local manifest
#
# Usage:
#   python tools/update-client.py <store_dir> <install_dir> [--channel stable]
# =============================================================================

import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import artcade_crypto  # noqa: E402
from artcade_keytool import resolve_key  # noqa: E402

LOCAL_MANIFEST = ".artcade-manifest.json"


def _safe_rel(rel: str) -> bool:
    norm = rel.replace("\\", "/")
    if norm.startswith("/") or ".." in norm.split("/"):
        return False
    return os.path.isabs(norm) is False


def _read_json(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def update(store_dir: str, install_dir: str, channel: str = "stable") -> dict:
    channel_doc = _read_json(os.path.join(store_dir, f"channel-{channel}.json"))
    version = channel_doc["latest"]
    manifest = _read_json(os.path.join(store_dir, channel_doc["manifest"]))
    remote_files: dict[str, dict] = manifest["files"]

    key, _ = resolve_key()
    os.makedirs(install_dir, exist_ok=True)
    local_manifest_path = os.path.join(install_dir, LOCAL_MANIFEST)
    local_files: dict[str, dict] = {}
    if os.path.exists(local_manifest_path):
        local_files = _read_json(local_manifest_path).get("files", {})

    # 1. Which files actually need fetching?
    needed: list[tuple[str, str]] = []
    for rel, info in remote_files.items():
        if not _safe_rel(rel):
            raise SystemExit(f"[FAIL] unsafe path in manifest: {rel}")
        local = local_files.get(rel)
        on_disk = os.path.exists(os.path.join(install_dir, rel))
        if local and local["sha256"] == info["sha256"] and on_disk:
            continue
        needed.append((rel, info["sha256"]))

    # 2. Fetch + decrypt + verify ALL needed objects before touching the install
    #    (all-or-nothing: a forged/corrupt object aborts with no partial apply).
    staged: dict[str, bytes] = {}
    for rel, digest in needed:
        obj_path = os.path.join(store_dir, "objects", digest)
        with open(obj_path, "rb") as f:
            container = f.read()
        try:
            data = artcade_crypto.decrypt(container, key)
        except Exception as exc:
            raise SystemExit(f"[FAIL] object {digest[:12]} for {rel} failed to decrypt: {exc}")
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"[FAIL] integrity mismatch for {rel}")
        staged[rel] = data

    # 3. Apply
    for rel, data in staged.items():
        target = os.path.join(install_dir, rel)
        os.makedirs(os.path.dirname(target) or install_dir, exist_ok=True)
        tmp = f"{target}.tmp.{os.getpid()}"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, target)

    # 4. Remove files no longer in the manifest
    removed: list[str] = []
    for rel in list(local_files.keys()):
        if rel not in remote_files:
            p = os.path.join(install_dir, rel)
            if os.path.exists(p):
                os.remove(p)
            removed.append(rel)

    # 5. Record the new local manifest
    with open(local_manifest_path, "w", encoding="utf-8") as f:
        json.dump({"version": version, "channel": channel, "files": remote_files},
                  f, indent=2)

    print(f"[OK] updated to {version} ({channel}): "
          f"{len(staged)} files transferred, {len(removed)} removed")
    return {"version": version,
            "transferred": [rel for rel, _ in needed],
            "removed": removed}


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply a delta update from a release store")
    parser.add_argument("store_dir")
    parser.add_argument("install_dir")
    parser.add_argument("--channel", default="stable")
    args = parser.parse_args()
    update(args.store_dir, args.install_dir, args.channel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
