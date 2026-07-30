add_executable(game
    src/main.cpp
    src/app.cpp
    src/app_bootstrap.cpp
    src/app_loop.cpp
    src/app_project_lifecycle.cpp
    src/app_scene_lifecycle.cpp
    src/app_scene_invalidation.cpp
    src/app_scene_render.cpp
    render/editor-overlay-renderer.cpp
    render/scene_frame_builder.cpp
    render/frame_coordinator.cpp
    render/physics_debug_renderer.cpp
    render/tilemap-renderer.cpp
    render/parallax-renderer.cpp
    render/passes/grid_pass.cpp
    render/passes/debug_pass.cpp
    render/passes/scene_background_pass.cpp
    render/passes/scene_entities_pass.cpp
)
target_include_directories(game PRIVATE include render render/passes)
target_link_libraries(game PRIVATE
    # RU-03: gameplay_session.cpp/sprite_frame_resolve.cpp/text_value_formatter.cpp
    # moved into the artcade-gameplay-session library (runtime-cpp/CMakeLists.txt)
    # so the editor's Play facade can link the same GameplaySession instead of
    # re-deriving its dependency list a third time.
    artcade-gameplay-session
    artcade-world
    artcade-game-api
    artcade-lua-runtime
    artcade-logic-core
    artcade-logic-runtime
    artcade-script-runtime
    artcade-asset-system
    artcade-renderer
    artcade-physics
    artcade-input
    artcade-audio
    artcade-scene-system
    artcade-runtime-entity-gateway
    # Stateless modules
    artcade-time
    artcade-event-bus
    artcade-variable-manager
    artcade-game-state
    artcade-texture-manager
    artcade-sprite-animator
    artcade-camera-manager
    artcade-tween-manager
    artcade-save-load
    artcade-dialog
)

# Native compiler warnings
if(MSVC)
    target_compile_options(game PRIVATE /W4 /permissive-)
else()
    target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ADR-0019: sidecar next to game.exe for refresh-export-templates.bat
if(ARTCADE_HAS_CRYPTO AND EXISTS "${ARTCADE_GENERATED_INCLUDE_DIR}/artcade-asset-key.h")
    file(STRINGS "${ARTCADE_GENERATED_INCLUDE_DIR}/artcade-asset-key.h" _key_id_line
         REGEX "kArtcadeAssetKeyId")
    string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" _ARTCADE_ASSET_KEY_ID "${_key_id_line}")
else()
    set(_ARTCADE_ASSET_KEY_ID "artcade-dev-key-v1")
endif()
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DINFO_OUT=$<TARGET_FILE_DIR:game>/runtime-build-info.json"
        "-DASSET_KEY_ID=${_ARTCADE_ASSET_KEY_ID}"
        "-DRUNTIME_BUILD_ID=${ARTCADE_RUNTIME_BUILD_ID}"
        "-DPLATFORMER_GROUND_SUPPORT=${ARTCADE_PLATFORMER_GROUND_SUPPORT}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/write-runtime-build-info.cmake"
    COMMENT "Writing runtime-build-info.json beside game.exe"
    VERBATIM
)
