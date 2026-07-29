cmake_minimum_required(VERSION 3.20)

if(EMSCRIPTEN)
    message(FATAL_ERROR "[ArtCade-Runtime] WASM export was retired with Studio V2.")
endif()

# ============================================================================
# MSVC runtime policy. Standalone / editor-linked runtime targets use /MT.
# CMP0091 NEW enables the CMAKE_MSVC_RUNTIME_LIBRARY policy.
# Emscripten is not MSVC, so the guard is safe on all platforms.
# ============================================================================
if(NOT EMSCRIPTEN)
    cmake_policy(SET CMP0091 NEW)
    if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
    endif()
endif()

project(ArtCade-Runtime CXX C)

# The native player is produced separately for the editor's Windows export
# template. The RmlUi editor itself links engine libraries only.
option(ARTCADE_BUILD_NATIVE_PLAYER
    "Build the native Windows export player" OFF)

# Root of this runtime tree — use instead of CMAKE_SOURCE_DIR in submodules when
# the runtime is add_subdirectory'd from another project (e.g. artcade-editor).
set(ARTCADE_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Native platform
# ============================================================================
message(STATUS "[ArtCade] Target: Native (${CMAKE_SYSTEM_NAME})")
set(ARTCADE_PLATFORM_WASM FALSE)

# ============================================================================
# Third-party libraries — guarded: only added if the directory exists.
# Fase 3 (test senza Raylib) passa anche senza libs/.
# ============================================================================
set(ARTCADE_HAS_RAYLIB FALSE)
set(ARTCADE_HAS_LUA    FALSE)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libs/raylib/CMakeLists.txt)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES       OFF CACHE BOOL "" FORCE)
    add_subdirectory(libs/raylib EXCLUDE_FROM_ALL)
    set(ARTCADE_HAS_RAYLIB TRUE)
    message(STATUS "[ArtCade] Raylib        : FOUND")
else()
    message(STATUS "[ArtCade] Raylib        : NOT FOUND — renderer/texture/input/audio skipped")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libs/lua/CMakeLists.txt)
    add_subdirectory(libs/lua EXCLUDE_FROM_ALL)
    set(ARTCADE_HAS_LUA TRUE)
    message(STATUS "[ArtCade] Lua           : FOUND")
else()
    message(STATUS "[ArtCade] Lua           : NOT FOUND — lua-runtime/game-api skipped")
endif()

# ----------------------------------------------------------------------------
# Asset encryption — Monocypher (XChaCha20-Poly1305) + build-time key header.
# Optional: when absent, artcade-crypto compiles as a passthrough and only
# plaintext .artcade archives load (dev convenience). Run
# scripts/bootstrap-runtime-libs.ps1 to fetch Monocypher.
# ----------------------------------------------------------------------------
set(ARTCADE_HAS_CRYPTO FALSE)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libs/monocypher/CMakeLists.txt)
    add_subdirectory(libs/monocypher EXCLUDE_FROM_ALL)
    set(ARTCADE_HAS_CRYPTO TRUE)
    message(STATUS "[ArtCade] Monocypher    : FOUND")

    find_program(ARTCADE_PYTHON NAMES python python3)
    set(ARTCADE_GENERATED_INCLUDE_DIR ${CMAKE_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${ARTCADE_GENERATED_INCLUDE_DIR})
    if(ARTCADE_PYTHON)
        # Regenerate at every configure; the script no-ops when content is
        # unchanged, so it does not force needless recompiles.
        execute_process(
            COMMAND ${ARTCADE_PYTHON}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/emit-asset-key-header.py
                    ${ARTCADE_GENERATED_INCLUDE_DIR}/artcade-asset-key.h
            RESULT_VARIABLE _key_rc)
        if(NOT _key_rc EQUAL 0)
            message(FATAL_ERROR "[ArtCade] Failed to generate asset-key header (rc=${_key_rc})")
        endif()
    else()
        message(FATAL_ERROR "[ArtCade] Python not found — required to generate the asset-key header")
    endif()
else()
    message(STATUS "[ArtCade] Monocypher    : NOT FOUND — .artcade encryption disabled (plaintext only)")
endif()

# Percorsi opzionali (non causano errori CMake se le dir non esistono)
set(SOL2_INCLUDE_DIR          ${CMAKE_CURRENT_SOURCE_DIR}/libs/sol2/include)
set(NLOHMANN_JSON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/nlohmann-json/include)
set(LUA_INCLUDE_DIR           ${CMAKE_CURRENT_SOURCE_DIR}/libs/lua)

# ============================================================================
# Shared core — LogicValue / NumberExpression storage (ADR-0028)
# ============================================================================
add_library(artcade-core STATIC
    src/core/logic-number-expression.cpp
    src/core/color-hex.cpp
    src/core/text-component-format.cpp
)
target_include_directories(artcade-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/sfx/include
)
foreach(_inc IN ITEMS
        ${SOL2_INCLUDE_DIR}
        ${NLOHMANN_JSON_INCLUDE_DIR})
    if(EXISTS ${_inc})
        target_include_directories(artcade-core PUBLIC ${_inc})
    endif()
endforeach()

add_library(artcade-object-type-materialize STATIC
    src/core/object-type-materialize.cpp
)
target_link_libraries(artcade-object-type-materialize PUBLIC artcade-core)

add_library(artcade-project-json STATIC
    src/core/project-current-format.cpp
    src/core/project-global-variables-format.cpp
    src/core/json-primitives.cpp
    src/core/collision-json.cpp
    src/core/physics-json.cpp
    src/core/sprite-json.cpp
    src/core/entity-json.cpp
    src/core/scene-json.cpp
    src/core/project-meta-json.cpp
    src/core/asset-json.cpp
)
target_link_libraries(artcade-project-json PUBLIC artcade-core)

# Deterministic Logic Board authoring model validation/compiler. This module is
# core-only: no Lua, renderer, filesystem, or editor presentation dependency.
add_subdirectory(src/modules/logic-core)
# RU-01: entity-json.cpp's read_object_type_logic_boards() (shared by
# AssetLoader::parseProjectJson and the editor's canonical project reader)
# needs Logic::logicBoardFromJson/validateBoard - added here, after
# artcade-logic-core exists, rather than reordering artcade-project-json's
# own add_library() above.
target_link_libraries(artcade-project-json PUBLIC artcade-logic-core)
add_subdirectory(src/modules/script-core)
add_subdirectory(src/modules/sprite-animation-core)
add_subdirectory(src/modules/sfx)

# ADR-0019: archive crypto/ZIP/SHA-256 (no Raylib/Lua) + project packer
add_subdirectory(src/modules/artcade-archive)
add_subdirectory(src/modules/artcade-pack)

# ============================================================================
# Moduli stateless — nessuna dipendenza da Raylib o Lua.
# Sempre compilati (Fase 3 e oltre).
# ============================================================================
add_subdirectory(src/modules/time)
add_subdirectory(src/modules/event-bus)
add_subdirectory(src/modules/variable-manager)
add_subdirectory(src/modules/game-state)
add_subdirectory(src/modules/sprite-animator)
add_subdirectory(src/modules/camera-manager)
add_subdirectory(src/modules/tween-manager)
add_subdirectory(src/modules/save-load)
add_subdirectory(src/modules/presentation)

# ============================================================================
# Collision kernel — shared by World platformer + Physics solver
# ============================================================================
add_subdirectory(src/modules/collision)

# ============================================================================
# Physics — custom solver + raymath header; no link to Raylib target (Fase 12)
# ============================================================================
add_subdirectory(src/modules/physics)

# ============================================================================
# Moduli con dipendenza da Raylib (Fase 5+)
# ============================================================================
if(ARTCADE_HAS_RAYLIB)
    add_subdirectory(src/modules/renderer)
    add_subdirectory(src/modules/texture-manager)
    add_subdirectory(src/modules/input)
    add_subdirectory(src/modules/audio)
endif()

# ============================================================================
# Moduli con dipendenza da Lua (Fase 11+)
# ============================================================================
if(ARTCADE_HAS_LUA)
    add_subdirectory(src/modules/lua-runtime)
    add_subdirectory(src/modules/logic-runtime)
    add_subdirectory(src/modules/script-runtime)
endif()

# ============================================================================
# Moduli che richiedono sia Raylib sia Lua (Fase 9+)
# ============================================================================
if(ARTCADE_HAS_RAYLIB AND ARTCADE_HAS_LUA)
    add_subdirectory(src/modules/scene-system)
    add_subdirectory(src/modules/runtime-entity-gateway)
    add_subdirectory(src/modules/asset-system)
    add_subdirectory(src/modules/game-api)
    add_subdirectory(src/modules/dialog)
    add_subdirectory(src/world)

    # RU-03 (docs/PLAY_RUNTIME_UNIFICATION_ROADMAP.md, editor repo): GameplaySession
    # as a real, linkable library - previously only ever compiled as extra sources
    # baked into `game`/gameplay_tick_order_characterization_test. The editor's
    # Play facade links this directly instead of re-deriving the same dependency
    # list a third time. Dependency list mirrors the one already proven to link
    # by that test (tests/CMakeLists.txt).
    add_library(artcade-gameplay-session STATIC
        src/app/src/gameplay_session.cpp
        src/app/src/gameplay_session_seed.cpp
        src/app/render/sprite_frame_resolve.cpp
        src/app/render/text_value_formatter.cpp
        src/app/render/tilemap_component_resolve.cpp
    )
    target_include_directories(artcade-gameplay-session PUBLIC
        src/app/include
        src/app/render
    )
    target_include_directories(artcade-gameplay-session PRIVATE
        libs/lua-5.4/src)
    target_link_libraries(artcade-gameplay-session PUBLIC
        artcade-core
        artcade-world
        artcade-scene-system
        artcade-runtime-entity-gateway
        artcade-physics
        artcade-variable-manager
        artcade-sprite-animator
        artcade-logic-core
        artcade-logic-runtime
        artcade-script-runtime
        artcade-lua-runtime
        artcade-game-api
        artcade-camera-manager
        artcade-event-bus
        artcade-game-state
        artcade-time
        artcade-tween-manager
        artcade-audio
        artcade-input
        lua54
    )

    if(ARTCADE_BUILD_NATIVE_PLAYER)
        add_subdirectory(src/app)
    endif()
endif()

# ============================================================================
# Tests (opzionale — abilita con -DARTCADE_BUILD_TESTS=ON)
# ============================================================================
option(ARTCADE_BUILD_TESTS "Build unit tests" OFF)
if(ARTCADE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
