"""Fixture Demo Suite — functional smoke of the native editor.

Orchestrates isolated editor processes against a scratch fixture regenerated
from makeVisualFixtureProject(). Success means exit code 0 from self-checking
--shot-* postconditions (and --lifecycle-smoke). Screenshots are diagnostic
only; their presence alone is never treated as a pass.

Usage:
  python scripts/run_fixture_demo.py
  python scripts/run_fixture_demo.py --list
  python scripts/run_fixture_demo.py --only play
  python scripts/run_fixture_demo.py --keep-shots

See docs/FIXTURE_DEMO_SUITE.md.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys
import time
from dataclasses import dataclass

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "src" / "artcade-editor-native.exe"
OUT_DIR = ROOT / "build" / "fixture-demo"
SCRATCH_FIXTURE = OUT_DIR / "visual-fixture.artcade"
SHOTS_DIR = OUT_DIR / "shots"
DEFAULT_TIMEOUT_S = 120


@dataclass(frozen=True)
class Scenario:
    args: tuple[str, ...]
    requires_shot: bool = True


def _fixture_args(*extra: str) -> tuple[str, ...]:
    return ("--shot-project", str(SCRATCH_FIXTURE), *extra)


# Authoritative scenario matrix. Escape selects the Ground tilemap instance
# (index 3 after Player + two Coins) so Rectangle can arm.
SCENARIOS: dict[str, Scenario] = {
    "gallery": Scenario(args=("--shot-gallery",)),
    "scene": Scenario(args=_fixture_args("--shot-entity", "0")),
    "logic": Scenario(
        args=_fixture_args("--shot-entity", "0", "--shot-logic"),
    ),
    "anim": Scenario(args=_fixture_args("--shot-anim")),
    "tileset": Scenario(args=_fixture_args("--shot-tileset")),
    "expression": Scenario(
        args=_fixture_args(
            "--shot-entity",
            "0",
            "--shot-logic",
            "--shot-expression",
            "rule-place|a|0|position|x",
            "--shot-expression-text",
            "ra",
        ),
    ),
    "escape": Scenario(
        args=_fixture_args("--shot-entity", "3", "--shot-escape"),
    ),
    "deselect": Scenario(
        args=_fixture_args("--shot-entity", "0", "--shot-deselect"),
    ),
    "pan_zoom": Scenario(
        args=_fixture_args("--shot-pan", "37,-19", "--shot-zoom", "2"),
    ),
    "play": Scenario(args=_fixture_args("--shot-play")),
    "asset_menu": Scenario(
        args=_fixture_args("--shot-asset-menu", "image|fixture-sheet"),
    ),
    "lifecycle": Scenario(args=("--lifecycle-smoke",), requires_shot=False),
}


def _tail(text: str, limit: int = 2000) -> str:
    if not text:
        return ""
    return text[-limit:]


def ensure_editor_exe() -> None:
    if not EXE.exists():
        sys.exit(f"editor not built: {EXE}\nbuild artcade-editor-native first")


def ensure_scratch_fixture() -> None:
    ensure_editor_exe()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [str(EXE), "--write-fixture", str(SCRATCH_FIXTURE)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=DEFAULT_TIMEOUT_S,
    )
    if result.returncode != 0 or not SCRATCH_FIXTURE.exists():
        sys.exit(
            "write-fixture failed:\n"
            + _tail(result.stdout or "")
            + _tail(result.stderr or "")
        )
    sheet = OUT_DIR / "visual-assets" / "design-system-sheet.png"
    if not sheet.is_file():
        sys.exit(f"write-fixture did not produce asset sheet: {sheet}")


def run_scenario(
    name: str,
    scenario: Scenario,
    *,
    keep_shots: bool,
) -> int:
    SHOTS_DIR.mkdir(parents=True, exist_ok=True)
    scratch_name = f"fixture-demo-{os.getpid()}-{name}.png"
    scratch = ROOT / scratch_name
    if scratch.exists():
        scratch.unlink()

    cmd = [str(EXE)]
    if scenario.requires_shot:
        cmd.extend(["--shot", scratch_name])
    cmd.extend(scenario.args)

    started = time.monotonic()
    try:
        result = subprocess.run(
            cmd,
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=DEFAULT_TIMEOUT_S,
        )
    except subprocess.TimeoutExpired as exc:
        print(f"FAIL [{name}] timed out after {DEFAULT_TIMEOUT_S}s")
        if exc.stdout:
            print(_tail(exc.stdout if isinstance(exc.stdout, str) else exc.stdout.decode()))
        if exc.stderr:
            print(_tail(exc.stderr if isinstance(exc.stderr, str) else exc.stderr.decode()))
        if scratch.exists():
            failed = SHOTS_DIR / f"{name}.fail.png"
            scratch.replace(failed)
            print(f"      kept failed shot: {failed}")
        return 1

    elapsed = time.monotonic() - started
    out_tail = _tail(result.stdout or "") + _tail(result.stderr or "")

    if result.returncode != 0:
        print(f"FAIL [{name}] exit {result.returncode} ({elapsed:.1f}s)")
        if out_tail:
            print(out_tail)
        if scratch.exists():
            failed = SHOTS_DIR / f"{name}.fail.png"
            scratch.replace(failed)
            print(f"      kept failed shot: {failed}")
        return 1

    if scenario.requires_shot:
        if not scratch.exists():
            print(f"FAIL [{name}] exit 0 but screenshot missing ({elapsed:.1f}s)")
            if out_tail:
                print(out_tail)
            return 1
        if keep_shots:
            kept = SHOTS_DIR / f"{name}.png"
            if kept.exists():
                kept.unlink()
            scratch.replace(kept)
        else:
            scratch.unlink(missing_ok=True)

    print(f"OK   [{name}] ({elapsed:.1f}s)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="ArtCade Fixture Demo Suite")
    parser.add_argument(
        "--list",
        action="store_true",
        help="list scenarios and exit (no build or fixture required)",
    )
    parser.add_argument(
        "--only",
        choices=sorted(SCENARIOS),
        help="run a single scenario",
    )
    parser.add_argument(
        "--keep-shots",
        action="store_true",
        help="keep all screenshots under build/fixture-demo/shots/",
    )
    args = parser.parse_args()

    if args.list:
        for name in sorted(SCENARIOS):
            scenario = SCENARIOS[name]
            shot = "shot" if scenario.requires_shot else "no-shot"
            print(f"{name}\t{shot}\t{' '.join(scenario.args)}")
        return 0

    ensure_editor_exe()

    names = [args.only] if args.only else sorted(SCENARIOS.keys())
    needs_fixture = any(
        "--shot-project" in SCENARIOS[n].args for n in names
    )
    if needs_fixture:
        ensure_scratch_fixture()

    failures = 0
    for name in names:
        failures += run_scenario(name, SCENARIOS[name], keep_shots=args.keep_shots)

    if failures:
        print(f"\nFixture Demo Suite: {failures} failed")
        return 1
    print(f"\nFixture Demo Suite: {len(names)} passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    finally:
        # Best-effort cleanup of orphaned scratch PNGs from this process.
        for orphan in ROOT.glob(f"fixture-demo-{os.getpid()}-*.png"):
            try:
                orphan.unlink()
            except OSError:
                pass
