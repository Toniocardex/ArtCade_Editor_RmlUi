# ArtCade Editor (native)

Native desktop editor for ArtCade — **RmlUi + Raylib + C++17**.

This is the **only** supported authoring UI. Do not add obsolete UI stacks here.

## Layout

```
artcade-editor/
├── src/editor-native/     # UI (RmlUi), viewport, authoring core
├── tests/                 # editor_core_test (no GL)
├── docs/                  # architecture, gates, contracts
├── scripts/               # build.bat, run.bat
└── vendor/artcade-runtime # Canonical local ArtCade C++ engine
```

## Requirements

- Windows x64, MSVC (Visual Studio 2022 Build Tools or full IDE)
- CMake 3.20+, Ninja on `PATH`
- Network on **first** configure (CMake FetchContent: RmlUi 6.1, FreeType 2.13.3)
- `vendor/artcade-runtime` is the canonical, versioned runtime source for this
  product. It is not a junction, submodule, or dependency on Studio V2.

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

Authoritative design rules (precedence): Constitution → Architecture → Engineering Gates.

## Project format

Loads/saves `.artcade-project` / project JSON compatible with the ArtCade engine. Schema changes must stay aligned with `vendor/artcade-runtime` `core/types.h` and the runtime JSON parsers.

## Related repos

| Repo | Role |
|------|------|
| **artcade-editor** (this) | Native RmlUi editor and canonical C++ runtime |
