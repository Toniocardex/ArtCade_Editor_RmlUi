# ArtCade Editor — agent notes

**Product:** native RmlUi editor only. Do not add React, Tauri, or WASM bridge code here.

**Runtime dependency:** `vendor/artcade-runtime` (ArtCade `runtime-cpp`). Link engine modules from there; never copy `editor-api` (WASM bridge).

**Authoring contract:** [`docs/RMLUI_MIGRATION_CONTRACT.md`](docs/RMLUI_MIGRATION_CONTRACT.md) overrides legacy React patterns when porting features.

**Build:** `scripts\build.bat` → `build\src\artcade-editor-native.exe`

**Tests:** `scripts\build.bat --test` runs `editor_core_test` (core logic, no GL).
