cmake_minimum_required(VERSION 3.20)

if(NOT EMSCRIPTEN)
    cmake_policy(SET CMP0091 NEW)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
    endif()
endif()

project(ArtCade-Editor CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ARTCADE_RUNTIME_DIR ${CMAKE_SOURCE_DIR}/vendor/artcade-runtime)
if(NOT EXISTS "${ARTCADE_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "[ArtCade-Editor] Missing vendor/artcade-runtime.\n"
        "  Clone the ArtCade runtime repo or create a junction:\n"
        "    mklink /J vendor\\artcade-runtime ..\\ArtCade-Studio_V2\\runtime-cpp")
endif()

option(ARTCADE_REQUIRE_EXPORT_TEMPLATES
    "Fail configuration when shipping export templates are missing" OFF)
set(_ARTCADE_EXPORT_TEMPLATE_EXE
    "${CMAKE_SOURCE_DIR}/src/editor-native/resources/export-templates/windows-x64/game.exe")
set(_ARTCADE_EXPORT_TEMPLATE_JSON
    "${CMAKE_SOURCE_DIR}/src/editor-native/resources/export-templates/windows-x64/runtime-template.json")
if(ARTCADE_REQUIRE_EXPORT_TEMPLATES)
    if(NOT EXISTS "${_ARTCADE_EXPORT_TEMPLATE_EXE}" OR NOT EXISTS "${_ARTCADE_EXPORT_TEMPLATE_JSON}")
        message(FATAL_ERROR
            "[ArtCade-Editor] ARTCADE_REQUIRE_EXPORT_TEMPLATES=ON but windows-x64 "
            "export template is missing. Run scripts/refresh-export-templates.bat")
    endif()
endif()

# Engine libraries only — this repo ships the native RmlUi editor, not WASM/React.
set(ARTCADE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARTCADE_BUILD_SMOKE_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(${ARTCADE_RUNTIME_DIR} ${CMAKE_BINARY_DIR}/_runtime)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
