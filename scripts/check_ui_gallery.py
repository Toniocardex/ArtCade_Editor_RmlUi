"""ADR-0027 phase 4 — visual regression for the component gallery.

Renders the specimen sheet with the real editor and compares it against the
committed reference. This is the check the C++ suites and the phase-3 token
test structurally cannot make: one verifies logic, the other verifies where
colours live — neither notices that a control stopped *looking* right. Both UI
bugs fixed on 2026-07-25 were of that kind, as was the disabled-state loss the
migration itself caused.

  python scripts/check_ui_gallery.py            # compare against the reference
  python scripts/check_ui_gallery.py --update   # accept the current rendering

Exit code 0 when the rendering matches, 1 otherwise (and it writes the actual
and diff images next to the reference so the change can be inspected).
"""
import argparse
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "src" / "artcade-editor-native.exe"
REFERENCE = ROOT / "tests" / "reference" / "ui-gallery.png"
ACTUAL = ROOT / "tests" / "reference" / "ui-gallery.actual.png"
DIFF = ROOT / "tests" / "reference" / "ui-gallery.diff.png"

# The capture is a real GPU render, so a handful of pixels may differ by a
# quantisation step between drivers. Anything a human would notice — a colour
# role, a lost state, a reflow — moves far more than this.
CHANNEL_TOLERANCE = 8
MAX_DIFFERING_PIXELS = 200


def render(target: pathlib.Path) -> None:
    if not EXE.exists():
        sys.exit(f"editor not built: {EXE}\nbuild artcade-editor-native first")
    target.parent.mkdir(parents=True, exist_ok=True)
    # raylib's TakeScreenshot writes to the working directory under the given
    # name, ignoring any directory part — so capture beside the repo root and
    # move the file where it belongs.
    scratch = ROOT / "ui-gallery.capture.png"
    if scratch.exists():
        scratch.unlink()
    result = subprocess.run(
        [str(EXE), "--shot", scratch.name, "--shot-gallery"],
        cwd=ROOT, capture_output=True, text=True)
    if not scratch.exists():
        sys.exit("capture failed:\n" + result.stdout[-2000:] + result.stderr[-2000:])
    if target.exists():
        target.unlink()
    scratch.replace(target)


def compare(reference: pathlib.Path, actual: pathlib.Path) -> int:
    try:
        from PIL import Image, ImageChops
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    ref = Image.open(reference).convert("RGB")
    cur = Image.open(actual).convert("RGB")
    if ref.size != cur.size:
        print(f"FAIL size changed: reference {ref.size} vs actual {cur.size}")
        print("      a layout change this large is never accidental — review it")
        return 1

    diff = ImageChops.difference(ref, cur)
    differing = [
        (x, y) for x, y in
        ((x, y) for y in range(diff.height) for x in range(diff.width))
        if max(diff.getpixel((x, y))) > CHANNEL_TOLERANCE
    ]
    if len(differing) <= MAX_DIFFERING_PIXELS:
        print(f"OK  gallery matches the reference "
              f"({len(differing)} pixels differ, tolerated {MAX_DIFFERING_PIXELS})")
        return 0

    # Where, not just how many: the caption under each specimen names its state,
    # so the bounding box points straight at the component that moved.
    xs = [p[0] for p in differing]
    ys = [p[1] for p in differing]
    print(f"FAIL {len(differing)} pixels differ from the reference")
    print(f"     bounding box x {min(xs)}–{max(xs)}, y {min(ys)}–{max(ys)}")
    diff.save(DIFF)
    print(f"     actual: {ACTUAL}")
    print(f"     diff:   {DIFF}")
    print("     if the change is intended: python scripts/check_ui_gallery.py --update")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true",
                        help="replace the reference with the current rendering")
    args = parser.parse_args()

    if args.update:
        render(REFERENCE)
        print(f"reference updated: {REFERENCE}")
        return 0

    if not REFERENCE.exists():
        sys.exit(f"no reference yet — create it with --update ({REFERENCE})")
    render(ACTUAL)
    return compare(REFERENCE, ACTUAL)


if __name__ == "__main__":
    raise SystemExit(main())
