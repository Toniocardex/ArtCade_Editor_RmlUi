cmake_minimum_required(VERSION 3.20)

# Smoke tests require Raylib
if(NOT ARTCADE_HAS_RAYLIB)
    message(WARNING "[SmokeTests] Raylib not available -- smoke tests skipped")
    return()
endif()

# ---------------------------------------------------------------------------
# Emscripten link flags
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    # Copy shell.html into the binary dir so the --shell-file path has no
    # spaces. (The source tree lives under "ArtCade V2"; em++ splits paths at
    # spaces. The relative path from the Ninja build root avoids the issue.)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
        "${CMAKE_CURRENT_BINARY_DIR}/shell.html"
        COPYONLY
    )
    file(RELATIVE_PATH SHELL_HTML_REL
        "${CMAKE_BINARY_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/shell.html"
    )

    # ------------------------------------------------------------------
    # EXPORTED_FUNCTIONS: only symbols guaranteed to exist in every target.
    # set_square_pos / set_square_color are ST-4-only; we use
    # --export-if-defined (safe no-op when the symbol is absent) so they
    # appear in ST-4's .wasm but the linker doesn't error on ST-1/2/3.
    # ------------------------------------------------------------------
    set(WASM_HARD_EXPORTS
        "_main"
        "_editor_set_mode"
        "_editor_select_entity"
        "_editor_deselect"
        "_editor_load_project"
        "_editor_set_transform"
        "_malloc"
        "_free"
    )
    string(JOIN "," WASM_HARD_EXPORTS_STR ${WASM_HARD_EXPORTS})

    # Optional exports (present only in some targets)
    set(WASM_OPTIONAL_EXPORTS
        "set_square_pos"
        "set_square_color"
    )

    # Build --export-if-defined flags for optional symbols
    set(WASM_OPT_EXPORT_FLAGS "")
    foreach(_sym IN LISTS WASM_OPTIONAL_EXPORTS)
        list(APPEND WASM_OPT_EXPORT_FLAGS "-Wl,--export-if-defined,${_sym}")
    endforeach()
    string(JOIN " " WASM_OPT_EXPORT_FLAGS_STR ${WASM_OPT_EXPORT_FLAGS})

    set(WASM_LINK_FLAGS
        "-s USE_WEBGL2=1"
        "-s USE_GLFW=3"
        "-s ALLOW_MEMORY_GROWTH=1"
        "-s EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\",\"lengthBytesUTF8\",\"stringToUTF8\"]"
        "-s EXPORTED_FUNCTIONS=[${WASM_HARD_EXPORTS_STR}]"
        "${WASM_OPT_EXPORT_FLAGS_STR}"
        "--shell-file ${SHELL_HTML_REL}"
    )
    string(JOIN " " WASM_LINK_FLAGS_STR ${WASM_LINK_FLAGS})
endif()

# ---------------------------------------------------------------------------
# Helper macro
# ---------------------------------------------------------------------------
macro(add_smoke NAME SRC)
    add_executable(${NAME} ${SRC})
    target_link_libraries(${NAME} PRIVATE raylib artcade-core artcade-editor-api)

    if(EMSCRIPTEN)
        set_target_properties(${NAME} PROPERTIES
            SUFFIX ".html"
            LINK_FLAGS "${WASM_LINK_FLAGS_STR}"
        )
    elseif(WIN32)
        target_link_libraries(${NAME} PRIVATE winmm gdi32)
    elseif(APPLE)
        target_link_libraries(${NAME} PRIVATE
            "-framework Cocoa" "-framework IOKit" "-framework CoreFoundation")
    endif()

    message(STATUS "[SmokeTests] + ${NAME}")
endmacro()

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_smoke(smoke_1_render  smoke_1_render.cpp)
add_smoke(smoke_2_input   smoke_2_input.cpp)
add_smoke(smoke_3_notify  smoke_3_notify.cpp)
add_smoke(smoke_4_command smoke_4_command.cpp)
