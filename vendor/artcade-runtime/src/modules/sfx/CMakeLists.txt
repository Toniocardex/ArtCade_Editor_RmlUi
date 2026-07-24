add_library(artcade-sfx STATIC
    src/synthesizer.cpp
    src/presets.cpp
    src/wav_encoder.cpp
    src/recipe_json.cpp
    src/vorbis_encoder_stub.cpp
)

add_library(ArtCade::Sfx ALIAS artcade-sfx)

target_include_directories(artcade-sfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(artcade-sfx PUBLIC cxx_std_17)
target_link_libraries(artcade-sfx PUBLIC artcade-core)
target_compile_definitions(artcade-sfx PUBLIC ARTCADE_SFX_HAS_JSON=1)

if(MSVC)
    target_compile_options(artcade-sfx PRIVATE /W4 /permissive-)
else()
    target_compile_options(artcade-sfx PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# Preview is a separate adapter so linking the deterministic SFX core never
# pulls Raylib into headless editor-core tests or authoring model code.
if(ARTCADE_HAS_RAYLIB)
    add_library(artcade-sfx-raylib STATIC src/raylib_preview.cpp)
    add_library(ArtCade::SfxRaylib ALIAS artcade-sfx-raylib)
    target_link_libraries(artcade-sfx-raylib PUBLIC artcade-sfx raylib)
    target_compile_definitions(artcade-sfx-raylib PUBLIC ARTCADE_SFX_HAS_RAYLIB=1)
else()
    add_library(artcade-sfx-raylib STATIC src/raylib_preview_stub.cpp)
    add_library(ArtCade::SfxRaylib ALIAS artcade-sfx-raylib)
    target_link_libraries(artcade-sfx-raylib PUBLIC artcade-sfx)
endif()
