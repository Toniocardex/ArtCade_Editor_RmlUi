#!/usr/bin/env python3
"""Refresh runtime-template.json from game.exe + runtime-build-info.json (ADR-0019)."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--game-exe", required=True, type=Path)
    parser.add_argument("--build-info", required=True, type=Path)
    parser.add_argument("--template-json", required=True, type=Path)
    args = parser.parse_args()

    if not args.game_exe.is_file():
        print(f"[FAIL] missing game.exe: {args.game_exe}", file=sys.stderr)
        return 1
    if not args.build_info.is_file():
        print(f"[FAIL] missing build info: {args.build_info}", file=sys.stderr)
        return 1

    info = json.loads(args.build_info.read_text(encoding="utf-8"))
    for key in ("engineVersion", "runtimeBuildId", "projectFormatMin",
                "projectFormatMax", "assetKeyId"):
        if key not in info or info[key] in (None, ""):
            print(f"[FAIL] build info missing {key}", file=sys.stderr)
            return 1

    size = args.game_exe.stat().st_size
    digest = sha256_file(args.game_exe)
    template = {
        "schemaVersion": 1,
        "templateVersion": 1,
        "target": "windows-x64",
        "engineVersion": info["engineVersion"],
        "runtimeBuildId": info["runtimeBuildId"],
        "projectFormat": {
            "minimum": int(info["projectFormatMin"]),
            "maximum": int(info["projectFormatMax"]),
        },
        "entryPoint": "game.exe",
        "supportsEncryptedArtcade": True,
        "assetKeyId": info["assetKeyId"],
        "files": {
            "game.exe": {
                "size": size,
                "sha256": digest,
            }
        },
    }
    args.template_json.parent.mkdir(parents=True, exist_ok=True)
    args.template_json.write_text(
        json.dumps(template, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
