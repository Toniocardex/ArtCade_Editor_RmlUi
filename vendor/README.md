# Local runtime source

`artcade-runtime` is the canonical, versioned ArtCade C++ runtime for this
repository. It is intentionally local: no junction, submodule, or Studio V2
checkout is required.

The native RmlUi editor links its engine modules directly. The historical
Studio V2 `editor-api`/Web-WASM host is opt-in only through
`ARTCADE_BUILD_LEGACY_STUDIO_BRIDGE` while it is being retired; it is never
part of the editor build graph.

CMake fails fast if `vendor/artcade-runtime/CMakeLists.txt` is missing.
