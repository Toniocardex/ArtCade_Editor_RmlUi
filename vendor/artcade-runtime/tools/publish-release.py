#!/usr/bin/env python3
# =============================================================================
# publish-release.py — publish a project version to a content-addressed store
#
# A release = a versioned manifest + a content store of per-file objects,
# each object addressed by the SHA-256 of its PLAINTEXT and stored ENCRYPTED.
# Identical files across versions share one object automatically (dedup), so a
# client only ever downloads what actually changed.
#
#   store/
#     channel-<name>.json        { channel, latest, manifest }
#     manifests/<version>.json   { version, channel, files: {rel: {sha256,size}} }
#     objects/<sha256>           XChaCha20-Poly1305 container of the file bytes
#
# Usage:
#   python tools/publish-release.py <project_dir> <store_dir> --version 1.2.0
#                                   [--channel stable]
#
# Diff at the plaintext-file level (not whole-archive binary diff): the shipped
# archive is encrypted+compressed, so byte-diffing it saves nothing. See
# docs/DELTA_UPDATES.md.
# =============================================================================

import argparse
import hashlib
import importlib.util
import json
import os
import sys
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import artcade_crypto  # noqa: E402
from artcade_keytool import resolve_key  # noqa: E402

# Reuse the packer's file collection + exclusion rules (single source of truth).
_spec = importlib.util.spec_from_file_location(
    "pack_artcade", os.path.join(HERE, "pack-artcade.py"))
_pack = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_pack)


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _atomic_write(path: str, data: bytes) -> None:
    tmp = f"{path}.tmp.{os.getpid()}"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def publish(project_dir: str, store_dir: str, version: str,
            channel: str = "stable") -> bool:
    project_dir = os.path.abspath(project_dir)
    if not os.path.exists(os.path.join(project_dir, "project.json")):
        print(f"[FAIL] project.json not found in: {project_dir}", file=sys.stderr)
        return False

    key, _ = resolve_key()
    objects_dir = os.path.join(store_dir, "objects")
    manifests_dir = os.path.join(store_dir, "manifests")
    os.makedirs(objects_dir, exist_ok=True)
    os.makedirs(manifests_dir, exist_ok=True)

    files = _pack.collect_files(project_dir)
    manifest_files: dict[str, dict] = {}
    new_objects = 0
    for full, rel in files:
        with open(full, "rb") as f:
            data = f.read()
        digest = _sha256(data)
        manifest_files[rel] = {"sha256": digest, "size": len(data)}
        obj_path = os.path.join(objects_dir, digest)
        if not os.path.exists(obj_path):
            _atomic_write(obj_path, artcade_crypto.encrypt(data, key))
            new_objects += 1

    manifest = {
        "format": "artcade-release",
        "version": version,
        "channel": channel,
        "created": datetime.now(timezone.utc).isoformat(),
        "files": manifest_files,
    }
    _atomic_write(os.path.join(manifests_dir, f"{version}.json"),
                  json.dumps(manifest, indent=2, ensure_ascii=False).encode("utf-8"))

    channel_doc = {
        "channel": channel,
        "latest": version,
        "manifest": f"manifests/{version}.json",
    }
    _atomic_write(os.path.join(store_dir, f"channel-{channel}.json"),
                  json.dumps(channel_doc, indent=2).encode("utf-8"))

    print(f"[OK] published {version} ({channel}): {len(files)} files, "
          f"{new_objects} new objects -> {store_dir}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Publish a project version to a release store")
    parser.add_argument("project_dir")
    parser.add_argument("store_dir")
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", default="stable")
    args = parser.parse_args()
    return 0 if publish(args.project_dir, args.store_dir, args.version, args.channel) else 1


if __name__ == "__main__":
    sys.exit(main())
