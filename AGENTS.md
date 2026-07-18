# ArtCade Editor — agent notes

**Product:** native RmlUi editor only. Do not add obsolete UI stacks (or WASM bridge UI) here.

**Runtime dependency:** `vendor/artcade-runtime` (ArtCade `runtime-cpp`). Link engine modules from there; never copy `editor-api` (WASM bridge).

## Architecture (binding — precedence order)

1. [`docs/ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`](docs/ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md) — vincolante
2. [`docs/ARTCADE_RMLUI_ARCHITECTURE.md`](docs/ARTCADE_RMLUI_ARCHITECTURE.md) — design di riferimento
3. [`docs/ARTCADE_RMLUI_ENGINEERING_GATES.md`](docs/ARTCADE_RMLUI_ENGINEERING_GATES.md) — DoD, test, review
4. [`docs/RMLUI_MIGRATION_CONTRACT.md`](docs/RMLUI_MIGRATION_CONTRACT.md) — historical feature-port notes (subordinate)

Before any feature slice: define authority, Intent/Command, invariants, Undo, Play behaviour, and tests (Gates §4). P0 violations = stop-the-line.

## Build & test

**Build:** `scripts\build.bat` → `build\src\artcade-editor-native.exe`

**Tests:** `scripts\build.bat --test` runs `editor_core_test` (core logic, no GL).
