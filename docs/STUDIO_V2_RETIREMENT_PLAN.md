# Studio V2 retirement plan

**Authority:** `vendor/artcade-runtime` is the canonical C++ engine source in
this repository. `src/editor-native` is the sole authoring product. The
runtime must remain independently buildable and must never depend on RmlUi or
editor-only targets.

## Current boundary

The RmlUi root forces `ARTCADE_BUILD_LEGACY_STUDIO_BRIDGE=OFF`. It therefore
links `GameplaySession` and engine modules directly, without configuring the
historical `editor-api` module or `src/app` Studio host. The latter is an
explicit compatibility build only, enabled by the runtime's native and WASM
build scripts until exported-game replacement is complete.

## Deletion gates

Delete `src/modules/editor-api` and its Web/WASM bridge only when all of these
conditions hold:

1. The standalone native game host uses explicit runtime host ports rather
   than `EditorAPI` globals, callbacks, or editor-mode state.
2. The exported game is built and smoke-tested without
   `ARTCADE_BUILD_LEGACY_STUDIO_BRIDGE`.
3. The old Web/WASM editor preview and WASM export have been removed. No
   Emscripten configuration or preview artifact may remain.
4. Runtime dependency-guard tests prove no production runtime header/source
   includes `editor-api.h`, RmlUi, or an editor target.
5. `scripts\build.bat --test`, the standalone runtime tests, and relevant
   native/WASM export checks are green from a clean configuration.

After these gates, remove the bridge, its exports, legacy preview artifacts and
their build paths in one atomic change. Archive or delete the external Studio
V2 repository only after the removal is committed and all consumers have moved
to this repository; remote repository deletion is a separate, irreversible
owner action.
