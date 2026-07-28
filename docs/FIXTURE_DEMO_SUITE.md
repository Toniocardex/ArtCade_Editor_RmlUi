# Fixture Demo Suite

Functional smoke suite for the native ArtCade editor. It drives the real
`artcade-editor-native` binary through the same Intent / `handleAction` paths
used by interactive editing. It does **not** introduce a second mutation path,
demo hub, or project authority.

## Principle

**Screenshot present ≠ functional pass.**

Each requested `--shot-*` flag self-checks its final state inside the editor
and returns a non-zero exit code on failure. Screenshots are kept for diagnosis
only. The Python runner fails a scenario when:

1. the process exit code is non-zero, or
2. a required screenshot file is missing after exit 0.

## How to run

```bat
scripts\build.bat --fixture-demo
python scripts\run_fixture_demo.py
python scripts\run_fixture_demo.py --list
python scripts\run_fixture_demo.py --only play
python scripts\run_fixture_demo.py --keep-shots
```

Flags on `build.bat` may appear in any order (`--clean`, `--test`,
`--fixture-demo`).

Discovery of the scenario list is only:

```bat
python scripts\run_fixture_demo.py --list
```

There is no C++ `--fixture-demo` flag (avoids a second, stale scenario list).

## Fixture sources

| Consumer | Path |
|---|---|
| UI gallery pixel regression | committed `tests/reference/visual-fixture.artcade` |
| Fixture Demo Suite | scratch `build/fixture-demo/visual-fixture.artcade` |

Both are produced by `makeVisualFixtureProject()` via
`artcade-editor-native --write-fixture <path>`. The smoke runner always writes
under `build/fixture-demo/` (including `assets/`) and never modifies
`tests/reference/`.

## Scenario matrix

| Scenario | Essential args | Postcondition |
|---|---|---|
| `gallery` | `--shot-gallery` | gallery mounted in workspace |
| `scene` | scratch + `--shot-entity 0` | entity selected |
| `logic` | scratch + entity + `--shot-logic` | Logic Board workspace |
| `anim` | scratch + `--shot-anim` | animation editor open |
| `tileset` | scratch + `--shot-tileset` | tileset editor open |
| `expression` | same args as UI gallery expression view | expression field / completion |
| `escape` | scratch + tilemap entity (`--shot-entity 3`) + `--shot-escape` | Select tool; same tilemap selection kept |
| `deselect` | scratch + entity + `--shot-deselect` | empty selection |
| `pan_zoom` | scratch + `--shot-pan 37,-19` + `--shot-zoom 2` | `SceneViewState` matches |
| `play` | scratch + `--shot-play` | Play active |
| `asset_menu` | scratch + `--shot-asset-menu image\|fixture-sheet` | context menu open |
| `lifecycle` | `--lifecycle-smoke` | existing RmlUi lifecycle self-check |

Authoritative machine-readable list: `python scripts/run_fixture_demo.py --list`.

## Runner behaviour

- One isolated editor process per scenario (sequential)
- Regenerates scratch fixture once when any scenario needs `--shot-project`
- Timeout per process; unique scratch PNG names (`PID` + scenario)
- Checks **return code first**, then required PNG presence
- Failed screenshots kept under `build/fixture-demo/shots/*.fail.png`
- `--keep-shots` keeps all PNGs under `build/fixture-demo/shots/`

## Relation to `--test` / UI gallery

| Suite | Opt-in | What it proves |
|---|---|---|
| Fixture Demo Suite | `build.bat --fixture-demo` | Functional postconditions (GPU) |
| UI gallery pixel | `scripts/check_ui_gallery.py` | Pixel baselines vs reference |
| `build.bat --test` | already includes gallery pixel | Unit/routing tests **plus** GPU gallery |

`--test` already depends on GPU because it invokes the gallery script. Do not
describe `--test` as GPU-free. The Fixture Demo Suite stays a separate opt-in
so smoke can run without the full test matrix, and vice versa.

## Non-goals (MVP)

- In-app demo hub / auto-load at boot
- Export E2E packing
- Pixel diffs inside the Fixture Demo Suite
- Duplicating the scenario list in C++
- Writing under `tests/reference/` from the smoke runner
