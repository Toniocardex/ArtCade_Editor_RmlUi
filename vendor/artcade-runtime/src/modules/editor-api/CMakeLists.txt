add_library(artcade-editor-api STATIC
    src/editor-api.cpp
    src/editor-input-controller.cpp
    src/editor-spritesheet-preview.cpp
    src/editor-transform-gizmo.cpp
    src/project-doc-parser.cpp
)

target_include_directories(artcade-editor-api PUBLIC include)
target_link_libraries(artcade-editor-api PUBLIC artcade-core artcade-object-type-materialize artcade-project-json artcade-scene-system)

if(TARGET artcade-runtime-entity-gateway)
    target_link_libraries(artcade-editor-api PUBLIC artcade-runtime-entity-gateway)
endif()

# editor_reload_script() calls LuaHost::loadLuaSource() — needs the symbol.
if(TARGET artcade-lua-runtime)
    target_link_libraries(artcade-editor-api PUBLIC artcade-lua-runtime)
endif()

if(TARGET artcade-dialog)
    target_link_libraries(artcade-editor-api PUBLIC artcade-dialog)
endif()

if(TARGET artcade-sprite-animator)
    target_link_libraries(artcade-editor-api PUBLIC artcade-sprite-animator)
endif()
if(TARGET artcade-audio)
    target_link_libraries(artcade-editor-api PUBLIC artcade-audio)
endif()

if(EMSCRIPTEN)
    target_compile_definitions(artcade-editor-api PRIVATE ARTCADE_WASM)
    target_link_libraries(artcade-editor-api PUBLIC artcade-input artcade-presentation)
    if(TARGET artcade-renderer)
        target_link_libraries(artcade-editor-api PRIVATE artcade-renderer)
    endif()
    message(STATUS "[ArtCade] editor-api  : WASM — full bridge (EMSCRIPTEN_KEEPALIVE + EM_ASM)")
else()
    message(STATUS "[ArtCade] editor-api  : Native — no-op stubs only")
endif()
