# ============================================================================
# editor-native — parallel native C++ editor spike (RmlUi evaluation).
#
#   artcade-editor-core   authoring logic only — ProjectDocument, commands,
#                         intents, coordinator, invalidation, play session.
#                         No Raylib, no RmlUi. Always built; unit-tested.
#
#   artcade-editor-native the RmlUi host + window + viewport, behind
#                         option(ARTCADE_BUILD_NATIVE_EDITOR). Needs the Raylib
#                         target and a fetched RmlUi (+ FreeType). Default OFF so
#                         the normal runtime build and the web editor are never
#                         affected.
# ============================================================================

add_library(artcade-editor-core STATIC
    model/project_document.cpp
    model/generated_sfx_policy.cpp
    model/generated_sfx_preset_catalog.cpp
    model/project_io.cpp
    model/play_session.cpp
    model/box_collider_geometry.cpp
    model/box_collider_view.cpp
    model/scene_frame_snapshot.cpp
    model/sprite_render_view.cpp
    model/sprite_animation_slicing.cpp
    view/scene_grid.cpp
    view/scene_view_camera.cpp
    commands/box_collider_commands.cpp
    commands/linear_mover_commands.cpp
    commands/auto_destroy_commands.cpp
    commands/camera_target_commands.cpp
    commands/top_down_controller_commands.cpp
    commands/platformer_controller_commands.cpp
    commands/project_commands.cpp
    commands/image_asset_commands.cpp
    commands/audio_asset_commands.cpp
    commands/font_asset_commands.cpp
    commands/sprite_animation_commands.cpp
    commands/entity_commands.cpp
    commands/scene_commands.cpp
    commands/scene_layer_commands.cpp
    commands/sprite_presentation_commands.cpp
    app/editor_coordinator.cpp
    app/editor_coordinator_workspace.cpp
    app/editor_coordinator_tilemap.cpp
    app/editor_coordinator_ui.cpp
    app/asset_import.cpp
    app/hierarchy_actions.cpp
    app/inspector_actions.cpp
    app/inspector_commit.cpp
    ui/generated_sfx_editor_controller.cpp
    app/project_file.cpp
    app/project_load.cpp
)

# `core/...` and `editor-native/...` includes resolve from the src root.
target_include_directories(artcade-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..   # runtime-cpp/src
)
target_link_libraries(artcade-editor-core PUBLIC artcade-core)

# ----------------------------------------------------------------------------
# Native RmlUi editor target (opt-in).
# ----------------------------------------------------------------------------
option(ARTCADE_BUILD_NATIVE_EDITOR "Build the RmlUi native editor spike" OFF)

if(ARTCADE_BUILD_NATIVE_EDITOR)
    if(NOT TARGET raylib)
        message(FATAL_ERROR
            "[ArtCade] ARTCADE_BUILD_NATIVE_EDITOR requires Raylib (libs/raylib).")
    endif()

    set(ARTCADE_EDITOR_REQUIRED_FONTS
        resources/fonts/inter/Inter-Regular.ttf
        resources/fonts/inter/Inter-Medium.ttf
        resources/fonts/inter/Inter-SemiBold.ttf
        resources/fonts/inter/Inter-Bold.ttf
        resources/fonts/inter/LICENSE.txt
    )
    foreach(_font IN LISTS ARTCADE_EDITOR_REQUIRED_FONTS)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_font}")
            message(FATAL_ERROR "[ArtCade] Missing native editor font resource: ${_font}")
        endif()
    endforeach()

    include(FetchContent)

    # -- FreeType: RmlUi's default font engine. Trim every optional dependency
    #    so it builds standalone (no zlib/png/harfbuzz/brotli/bzip2 on the box).
    set(FT_DISABLE_HARFBUZZ ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG      ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB     ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2    ON  CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI   ON  CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(freetype)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()

    # -- RmlUi (pinned). No samples, static, FreeType font engine.
    # No samples → RmlUi builds only RmlCore + RmlDebugger. The native editor
    # compiles RmlUi's official GL3 renderer directly and uses raylib's glad
    # loader through RMLUI_GL3_CUSTOM_LOADER.
    set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
    set(RMLUI_SAMPLES       OFF CACHE BOOL "" FORCE)
    set(RMLUI_BACKEND       "auto" CACHE STRING "" FORCE)  # validated only; not built (samples off)
    set(RMLUI_FONT_ENGINE   "freetype" CACHE STRING "" FORCE)
    FetchContent_Declare(rmlui
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG        6.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(rmlui)

    set(ARTCADE_EDITOR_NATIVE_SOURCES
        app/main.cpp
        app/editor_app.cpp
        app/generated_sfx_generation_preflight.cpp
        app/generated_sfx_generation_service.cpp
        app/generated_sfx_status_projection.cpp
        app/generated_sfx_output_transaction.cpp
        app/project_session_controller.cpp
        app/scene_view_interaction.cpp
        ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp
        app/rml_system.cpp
        app/rml_host.cpp
        app/editor_input.cpp
        app/sprite_animation_canvas_input.cpp
        app/sprite_animation_preview_renderer.cpp
        app/file_dialog.cpp
        app/confirm_dialog.cpp
        view/texture_cache.cpp
        view/texture_request_catalog.cpp
        view/scene_view.cpp
        ui/editor_ui.cpp
        ui/logic_board_editor_controller.cpp
        ui/sprite_animation_editor_controller.cpp
        ui/tileset_editor_controller.cpp
        ui/editor_fonts.cpp
        ui/hierarchy_panel.cpp
        ui/inspector_panel.cpp
        ui/tile_palette_dock_panel.cpp
        ui/console_panel.cpp
        ui/assets_panel.cpp
    )
    if(WIN32)
        list(APPEND ARTCADE_EDITOR_NATIVE_SOURCES artcade_editor_native.rc)
    endif()

    add_executable(artcade-editor-native
        ${ARTCADE_EDITOR_NATIVE_SOURCES}
    )
    if(WIN32)
        set_target_properties(artcade-editor-native PROPERTIES
            WIN32_EXECUTABLE TRUE)
        if(MSVC)
            target_link_options(artcade-editor-native PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
    target_include_directories(artcade-editor-native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..   # runtime-cpp/src
        ${rmlui_SOURCE_DIR}/Backends
    )
    target_compile_definitions(artcade-editor-native PRIVATE
        RMLUI_GL3_CUSTOM_LOADER="external/glad.h"
    )
    target_link_libraries(artcade-editor-native PRIVATE
        artcade-editor-core
        raylib
        RmlUi::Core
        RmlUi::Debugger
    )
    if(WIN32)
        # comdlg32: GetOpenFileName/GetSaveFileName for the native file pickers.
        target_link_libraries(artcade-editor-native PRIVATE comdlg32)
    endif()

    # Copy RML/RCSS/fonts next to the executable. This is a real build dependency,
    # not POST_BUILD-only, so resource-only edits update the runnable native build.
    file(GLOB_RECURSE ARTCADE_EDITOR_RESOURCE_FILES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/*)
    set(ARTCADE_EDITOR_RESOURCE_STAMP
        ${CMAKE_CURRENT_BINARY_DIR}/artcade-editor-native-resources.stamp)
    add_custom_command(
        OUTPUT ${ARTCADE_EDITOR_RESOURCE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:artcade-editor-native>/resources
        COMMAND ${CMAKE_COMMAND} -E touch ${ARTCADE_EDITOR_RESOURCE_STAMP}
        DEPENDS ${ARTCADE_EDITOR_RESOURCE_FILES}
        COMMENT "[editor-native] copying RML/RCSS resources")
    add_custom_target(artcade-editor-native-resources
        DEPENDS ${ARTCADE_EDITOR_RESOURCE_STAMP})
    add_dependencies(artcade-editor-native artcade-editor-native-resources)

    message(STATUS "[ArtCade] Native editor : ENABLED (RmlUi 6.1 + FreeType)")
endif()
