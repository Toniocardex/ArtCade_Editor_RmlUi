# ArtCade Editor (native)

Native desktop editor for ArtCade — **RmlUi + Raylib + C++17**.

This repository is separate from the legacy React/Tauri editor. All new authoring UI work happens here.

## Layout

```
artcade-editor/
├── src/editor-native/     # UI (RmlUi), viewport, authoring core
├── tests/                 # editor_core_test (no GL)
├── docs/                  # RmlUi migration contract & plans
├── scripts/               # build.bat, run.bat
└── vendor/artcade-runtime # ArtCade C++ engine (submodule or junction)
```

## Requirements

- Windows x64, MSVC (Visual Studio 2022 Build Tools or full IDE)
- CMake 3.20+, Ninja on `PATH`
- Network on **first** configure (CMake FetchContent: RmlUi 6.1, FreeType 2.13.3)
- `vendor/artcade-runtime` must point at the ArtCade runtime tree (`runtime-cpp`)

### Link the runtime (pick one)

**Local junction (dev on one machine):**

```powershell
cmd /c mklink /J vendor\artcade-runtime ..\ArtCade-Studio_V2\runtime-cpp
```

**Git submodule (recommended for a second remote):**

```powershell
git submodule add <url-to-artcade-runtime-repo> vendor/artcade-runtime
git submodule update --init --recursive
```

## Build & run

```powershell
scripts\build.bat
scripts\run.bat
```

With unit tests:

```powershell
scripts\build.bat --test
```

Clean rebuild:

```powershell
scripts\build.bat --clean
```

Output: `build\src\artcade-editor-native.exe` (with `resources/` copied alongside).

## Architecture

- **`artcade-editor-core`** — `ProjectDocument`, commands, coordinator (no RmlUi/Raylib).
- **`artcade-editor-native`** — RmlUi shell, panels, GL viewport.

Authoritative design rules: [`docs/RMLUI_MIGRATION_CONTRACT.md`](docs/RMLUI_MIGRATION_CONTRACT.md).

## Project format

Loads/saves `.artcade-project` / project JSON compatible with the ArtCade engine. Schema changes must stay aligned with `vendor/artcade-runtime` `core/types.h` and the runtime JSON parsers.

## Related repos

| Repo | Role |
|------|------|
| **artcade-editor** (this) | Native RmlUi editor — active development |
| **ArtCade-Studio** | Game runtime + WASM + legacy React editor (maintenance) |
