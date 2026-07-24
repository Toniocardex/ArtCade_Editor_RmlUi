# Monocypher 4.0.2 ??? public-domain crypto (XChaCha20-Poly1305 AEAD).
# Single translation unit; compiles unchanged on MSVC and Emscripten.
add_library(monocypher STATIC monocypher.c)
target_include_directories(monocypher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
