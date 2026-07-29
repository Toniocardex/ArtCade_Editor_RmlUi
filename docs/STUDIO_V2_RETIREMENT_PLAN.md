# Studio V2 retirement plan

**Authority:** `vendor/artcade-runtime` is the canonical C++ engine source in
this repository. `src/editor-native` is the sole authoring product. The
runtime must remain independently buildable and must never depend on RmlUi or
editor-only targets.

## Completed boundary

The RmlUi root forces `ARTCADE_BUILD_NATIVE_PLAYER=OFF`, links engine modules
directly, and never configures an editor bridge. The runtime separately exposes
`ARTCADE_BUILD_NATIVE_PLAYER=ON` only for generating the Windows export
template. `src/modules/editor-api`, Studio V2 smoke tests, and Web/WASM build
paths have been removed.

## Remaining ownership

The external Studio V2 repository can be archived or deleted only by its
owner after this cleanup is committed. That remote operation is separate and
irreversible.
