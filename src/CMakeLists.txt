# ============================================================================
# ArtCade native editor (RmlUi).
#
#   artcade-editor-core   — authoring logic (ProjectDocument, commands, …).
#                         No Raylib, no RmlUi. Always built; unit-tested.
#
#   artcade-editor-native — RmlUi host + window + viewport (main product).
# ============================================================================

add_library(artcade-editor-core STATIC
    editor-native/model/project_document.cpp
    editor-native/model/project_io.cpp
    editor-native/model/path_confinement.cpp
    editor-native/model/play_session.cpp
    editor-native/model/box_collider_geometry.cpp
    editor-native/model/box_collider_view.cpp
    editor-native/model/scene_frame_snapshot.cpp
    editor-native/model/sprite_render_view.cpp
    editor-native/model/sprite_animation_slicing.cpp
    editor-native/model/tileset_slicing.cpp
    editor-native/model/tilemap_chunk_math.cpp
    editor-native/model/tilemap_validation.cpp
    editor-native/model/tilemap_render_view.cpp
    editor-native/model/tilemap_cell_access.cpp
    editor-native/model/tilemap_stroke_math.cpp
    editor-native/model/tilemap_stroke_preview.cpp
    editor-native/model/tilemap_region_math.cpp
    editor-native/view/scene_grid.cpp
    editor-native/view/scene_view_camera.cpp
    editor-native/commands/box_collider_commands.cpp
    editor-native/commands/linear_mover_commands.cpp
    editor-native/commands/top_down_controller_commands.cpp
    editor-native/commands/platformer_controller_commands.cpp
    editor-native/commands/project_commands.cpp
    editor-native/commands/image_asset_commands.cpp
    editor-native/commands/audio_asset_commands.cpp
    editor-native/commands/font_asset_commands.cpp
    editor-native/commands/tileset_commands.cpp
    editor-native/commands/tilemap_commands.cpp
    editor-native/commands/sprite_animation_commands.cpp
    editor-native/commands/logic_board_commands.cpp
    editor-native/commands/entity_commands.cpp
    editor-native/commands/scene_commands.cpp
    editor-native/commands/scene_layer_commands.cpp
    editor-native/commands/sprite_presentation_commands.cpp
    editor-native/app/editor_coordinator.cpp
    editor-native/app/editor_coordinator_workspace.cpp
    editor-native/app/editor_coordinator_tilemap.cpp
    editor-native/app/editor_coordinator_ui.cpp
    editor-native/app/asset_import.cpp
    editor-native/app/hierarchy_actions.cpp
    editor-native/app/inspector_actions.cpp
    editor-native/app/inspector_commit.cpp
    editor-native/app/pending_edit.cpp
    editor-native/app/project_file.cpp
    editor-native/app/project_load.cpp
    editor-native/app/new_project_transaction.cpp
)

target_include_directories(artcade-editor-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ARTCADE_RUNTIME_DIR}/src
)
target_link_libraries(artcade-editor-core PUBLIC
    artcade-core
    artcade-logic-core
    artcade-logic-runtime
)

# ----------------------------------------------------------------------------
# Native RmlUi editor executable
# ----------------------------------------------------------------------------
if(NOT TARGET raylib)
    message(FATAL_ERROR
        "[ArtCade-Editor] Raylib not found under vendor/artcade-runtime/libs/raylib.")
endif()

set(ARTCADE_EDITOR_REQUIRED_FONTS
    editor-native/resources/fonts/inter/Inter-Regular.ttf
    editor-native/resources/fonts/inter/Inter-Medium.ttf
    editor-native/resources/fonts/inter/Inter-SemiBold.ttf
    editor-native/resources/fonts/inter/Inter-Bold.ttf
    editor-native/resources/fonts/inter/LICENSE.txt
)
foreach(_font IN LISTS ARTCADE_EDITOR_REQUIRED_FONTS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_font}")
        message(FATAL_ERROR "[ArtCade-Editor] Missing font resource: ${_font}")
    endif()
endforeach()

include(FetchContent)

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

set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES       OFF CACHE BOOL "" FORCE)
set(RMLUI_BACKEND       "auto" CACHE STRING "" FORCE)
set(RMLUI_FONT_ENGINE   "freetype" CACHE STRING "" FORCE)
FetchContent_Declare(rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(rmlui)

set(ARTCADE_EDITOR_NATIVE_SOURCES
    editor-native/app/main.cpp
    editor-native/app/editor_app.cpp
    editor-native/app/project_session_controller.cpp
    editor-native/app/scene_view_interaction.cpp
    ${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp
    editor-native/app/rml_system.cpp
    editor-native/app/rml_host.cpp
    editor-native/app/editor_input.cpp
    editor-native/app/checker_pattern.cpp
    editor-native/app/sprite_animation_canvas_input.cpp
    editor-native/app/sprite_animation_preview_renderer.cpp
    editor-native/app/tileset_editor_canvas_input.cpp
    editor-native/app/tileset_editor_renderer.cpp
    editor-native/app/tile_palette_renderer.cpp
    editor-native/app/file_dialog.cpp
    editor-native/app/confirm_dialog.cpp
    editor-native/view/canvas_font.cpp
    editor-native/view/texture_cache.cpp
    editor-native/view/texture_request_catalog.cpp
    editor-native/view/scene_view.cpp
    editor-native/view/tilemap_paint_overlay.cpp
    editor-native/app/tilemap_paint_input.cpp
    editor-native/ui/ui_markup.cpp
    editor-native/ui/editor_ui.cpp
    editor-native/ui/logic_board_editor_controller.cpp
    editor-native/ui/sprite_animation_editor_controller.cpp
    editor-native/ui/tileset_editor_controller.cpp
    editor-native/ui/editor_fonts.cpp
    editor-native/ui/hierarchy_panel.cpp
    editor-native/ui/inspector_panel.cpp
    editor-native/ui/logic_board_panel.cpp
    editor-native/ui/console_panel.cpp
    editor-native/ui/assets_panel.cpp
)
if(WIN32)
    list(APPEND ARTCADE_EDITOR_NATIVE_SOURCES editor-native/artcade_editor_native.rc)
    # The Windows resource compiler does not reliably report the icon file as a
    # dependency, so track it explicitly. This makes icon-only updates rebuild
    # the executable instead of merely copying the runtime PNG resource.
    set_property(SOURCE editor-native/artcade_editor_native.rc APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/editor-native/resources/app-icon.ico")
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
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ARTCADE_RUNTIME_DIR}/src
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
    target_link_libraries(artcade-editor-native PRIVATE comdlg32)
endif()

file(GLOB_RECURSE ARTCADE_EDITOR_RESOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/editor-native/resources/*)
set(ARTCADE_EDITOR_RESOURCE_STAMP
    ${CMAKE_BINARY_DIR}/artcade-editor-native-resources.stamp)
add_custom_command(
    OUTPUT ${ARTCADE_EDITOR_RESOURCE_STAMP}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/editor-native/resources
        $<TARGET_FILE_DIR:artcade-editor-native>/resources
    COMMAND ${CMAKE_COMMAND} -E touch ${ARTCADE_EDITOR_RESOURCE_STAMP}
    DEPENDS ${ARTCADE_EDITOR_RESOURCE_FILES}
    COMMENT "[artcade-editor] copying RML/RCSS resources")
add_custom_target(artcade-editor-native-resources
    DEPENDS ${ARTCADE_EDITOR_RESOURCE_STAMP})
add_dependencies(artcade-editor-native artcade-editor-native-resources)

message(STATUS "[ArtCade-Editor] Native editor target: artcade-editor-native (RmlUi 6.1)")
