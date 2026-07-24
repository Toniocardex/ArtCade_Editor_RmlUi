add_executable(game
    src/main.cpp
    src/app.cpp
    src/app_bootstrap.cpp
    src/app_loop.cpp
    src/app_project_lifecycle.cpp
    src/app_scene_mutation.cpp
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
    render/passes/gizmo_pass.cpp
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
    artcade-editor-api          # WASM bridge (no-op stubs on native)
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

# ============================================================================
# Emscripten WASM
# ============================================================================
if(EMSCRIPTEN)
    # ARTCADE_WASM -> app.cpp uses emscripten_set_main_loop
    target_compile_definitions(game PRIVATE ARTCADE_WASM)

    # C++ exceptions required by Sol2
    target_compile_options(game PRIVATE -fexceptions -O2)

    # Output: game.js + game.wasm only (no .html — html-minifier-terser unreliable)
    set_target_properties(game PROPERTIES SUFFIX ".js")

    # Editor WASM preview: no test-project preload — projects come from the
    # Editor host via editor_load_project. (runtime-cpp/test-project remains
    # for native runs and smoke tests.)

    # EXPORTED_FUNCTIONS: main + all editor-api commands callable from ccall()
    set(GAME_EXPORTED_FUNCTIONS
        "_main"
        "_editor_is_engine_wired"
        "_editor_get_boot_failure"
        "_editor_get_main_exit_code"
        "_editor_set_mode"
        "_editor_select_entity"
        "_editor_deselect"
        "_editor_load_project"
        "_editor_restore_from_project"
        "_editor_enter_play_mode"
        "_editor_exit_play_mode"
        "_editor_set_transform"
        "_editor_update_entity"
        "_editor_set_scene_settings"
        "_editor_begin_authoring_sync_batch"
        "_editor_end_authoring_sync_batch"
        "_editor_reload_script"
        "_editor_load_dialogs"
        "_editor_set_tool"
        "_editor_set_guides_enabled"
        "_editor_set_grid_size"
        "_editor_set_snap_to_grid"
        "_editor_set_edit_camera"
        "_editor_resize_surface"
        "_editor_begin_pan"
        "_editor_update_pan"
        "_editor_end_pan"
        "_editor_zoom_at"
        "_editor_frame_world_bounds"
        "_editor_frame_selection"
        "_editor_get_editor_view"
        "_editor_set_editor_view"
        "_editor_get_presentation_revision"
        "_editor_get_scene_revision"
        "_editor_set_pointer_presentation_revision"
        "_editor_get_presentation_snapshot"
        "_editor_surface_to_world"
        "_editor_surface_to_world_at_revision"
        "_editor_sync_play_surface"
        "_editor_set_play_presentation"
        "_editor_register_image"
        "_editor_register_audio"
        "_editor_register_font"
        "_editor_invalidate_asset"
        "_editor_reregister_animation_clips"
        "_editor_preview_spritesheet_reset"
        "_editor_preview_spritesheet_submit"
        "_editor_get_runtime_profile"
        "_malloc"
        "_free"
    )
    string(JOIN "," _FUNS_STR ${GAME_EXPORTED_FUNCTIONS})

    target_link_options(game PRIVATE
        -fexceptions
        -sWASM=1
        -sUSE_GLFW=3
        # Render to an offscreen FBO and blit to the canvas each swap. Required so
        # WebView2 (Chromium/ANGLE) composites the re-parented singleton canvas —
        # without it the WebGL backbuffer never reaches the screen (black viewport)
        # and GL.resizeOffscreenFramebuffer (used by the editor's wakeRuntimeCanvasGl)
        # does not exist. See editor runtime-canvas.ts.
        -sOFFSCREEN_FRAMEBUFFER=1
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=0
        -sFORCE_FILESYSTEM=1
        "-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"lengthBytesUTF8\",\"stringToUTF8\",\"HEAPU8\",\"HEAPF32\",\"GL\"]"
        "-sEXPORTED_FUNCTIONS=[${_FUNS_STR}]"
        -sASYNCIFY=0
        -O2
    )
else()
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
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/write-runtime-build-info.cmake"
        COMMENT "Writing runtime-build-info.json beside game.exe"
        VERBATIM
    )
endif()
