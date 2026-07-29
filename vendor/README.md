# Local runtime source

`artcade-runtime` is the canonical, versioned ArtCade C++ runtime for this
repository. It is intentionally local: no junction, submodule, or Studio V2
checkout is required.

The native RmlUi editor links its engine modules directly. The native player
is an independent opt-in target (`ARTCADE_BUILD_NATIVE_PLAYER`) used only to
produce the Windows export template; it is never part of the editor build
graph.

CMake fails fast if `vendor/artcade-runtime/CMakeLists.txt` is missing.
