cmake_minimum_required(VERSION 3.20)

# Paths must use ARTCADE_RUNTIME_ROOT (set by runtime-cpp/CMakeLists.txt), not
# CMAKE_SOURCE_DIR — the latter is the monorepo root when runtime-cpp is an
# add_subdirectory of ArtCade V2, which breaks include and source paths.

# ============================================================================
# Helper
# ============================================================================
function(artcade_test TARGET SOURCES LIBS)
    add_executable(${TARGET} ${SOURCES})
    target_include_directories(${TARGET} PRIVATE ${ARTCADE_RUNTIME_ROOT}/src)
    target_link_libraries(${TARGET} PRIVATE ${LIBS})
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endfunction()

# ============================================================================
# Fase 1-2: moduli stateless (nessuna dipendenza da Raylib)
# Sempre compilati — il checkpoint di Fase 3 li esegue tutti.
# ============================================================================
artcade_test(time_manager_test
    time-manager-test.cpp
    artcade-time)

artcade_test(event_bus_test
    event-bus-test.cpp
    artcade-event-bus)

artcade_test(variable_manager_test
    variable-manager-test.cpp
    artcade-variable-manager)

add_executable(text_value_formatter_test
    text_value_formatter_test.cpp)
target_include_directories(text_value_formatter_test PRIVATE ${ARTCADE_RUNTIME_ROOT}/src)
target_link_libraries(text_value_formatter_test PRIVATE artcade-core artcade-variable-manager)
add_test(NAME text_value_formatter_test COMMAND text_value_formatter_test)

artcade_test(sprite_pivot_test
    sprite-pivot-test.cpp
    artcade-core)

artcade_test(object_type_materialize_test
    object-type-materialize-test.cpp
    artcade-object-type-materialize)

add_executable(tilemap_component_resolve_test
    tilemap-component-resolve-test.cpp
    ${ARTCADE_RUNTIME_ROOT}/src/app/render/tilemap_component_resolve.cpp)
target_include_directories(tilemap_component_resolve_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/app/render)
target_link_libraries(tilemap_component_resolve_test PRIVATE artcade-core)
add_test(NAME tilemap_component_resolve_test COMMAND tilemap_component_resolve_test)

artcade_test(asset_manifest_index_test
    asset-manifest-index-test.cpp
    artcade-asset-system)

artcade_test(physics_body_rules_test
    physics-body-rules-test.cpp
    artcade-runtime-entity-gateway)

artcade_test(game_state_test
    game-state-test.cpp
    "artcade-game-state;artcade-event-bus")

artcade_test(sprite_animator_test
    sprite-animator-test.cpp
    artcade-sprite-animator)

artcade_test(camera_manager_test
    camera-manager-test.cpp
    artcade-camera-manager)

artcade_test(tween_manager_test
    tween-manager-test.cpp
    artcade-tween-manager)

add_executable(compositor_layout_test
    compositor-layout-test.cpp
)
target_include_directories(compositor_layout_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
)
target_link_libraries(compositor_layout_test PRIVATE artcade-presentation)
add_test(NAME compositor_layout_test COMMAND compositor_layout_test)

artcade_test(presentation_coordinate_mapper_test
    presentation-coordinate-mapper-test.cpp
    artcade-presentation)

artcade_test(presentation_surface_metrics_test
    presentation-surface-metrics-test.cpp
    artcade-presentation)

artcade_test(presentation_system_test
    presentation-system-test.cpp
    artcade-presentation)

artcade_test(frame_selection_test
    frame-selection-test.cpp
    artcade-presentation)

artcade_test(editor_zoom_policy_test
    editor-zoom-policy-test.cpp
    artcade-presentation)

add_executable(presentation_integration_test
    presentation-integration-test.cpp
)
target_include_directories(presentation_integration_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/modules/renderer/include
    ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
)
target_link_libraries(presentation_integration_test PRIVATE artcade-renderer)
add_test(NAME presentation_integration_test COMMAND presentation_integration_test)

add_executable(presentation_camera_modes_test
    presentation-camera-modes-test.cpp
)
target_include_directories(presentation_camera_modes_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/modules/renderer/include
    ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
)
target_link_libraries(presentation_camera_modes_test PRIVATE artcade-renderer)
add_test(NAME presentation_camera_modes_test COMMAND presentation_camera_modes_test)

add_executable(render_pipeline_test render_pipeline_test.cpp)
target_include_directories(render_pipeline_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/modules/renderer/include
    ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
)
target_link_libraries(render_pipeline_test PRIVATE artcade-renderer)
add_test(NAME render_pipeline_test COMMAND render_pipeline_test)

add_executable(parallax_projection_test parallax-projection-test.cpp)
target_include_directories(parallax_projection_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/app/render
)
add_test(NAME parallax_projection_test COMMAND parallax_projection_test)

add_executable(presentation_golden_test
    presentation-golden-test.cpp
)
target_include_directories(presentation_golden_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
    ${ARTCADE_RUNTIME_ROOT}/src/modules/renderer/include
)
target_link_libraries(presentation_golden_test PRIVATE artcade-presentation)
add_test(NAME presentation_golden_test COMMAND presentation_golden_test)

artcade_test(save_load_test
    save-load-test.cpp
    "artcade-save-load;artcade-variable-manager")

artcade_test(dialog_parser_test
    dialog-parser-test.cpp
    artcade-dialog)
# Vendored tree is self-contained: docs/examples lives inside the runtime
# root itself (no parent repo above it since the 2026-07-25 repo split).
target_compile_definitions(dialog_parser_test
    PRIVATE ARTCADE_REPO_ROOT="${ARTCADE_RUNTIME_ROOT}")

# ============================================================================
# TextureManager — compilato con stub Raylib (funziona anche senza GPU).
# Le sorgenti vengono compilate direttamente (no link a artcade-texture-manager
# che richiederebbe il vero Raylib).
# ============================================================================
add_executable(texture_manager_test
    texture-manager-test.cpp
    ${ARTCADE_RUNTIME_ROOT}/src/modules/texture-manager/src/texture-manager.cpp
    stubs/raylib-stub.cpp
)
target_include_directories(texture_manager_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs   # stub/raylib.h ha priorità sul vero raylib.h
)
add_test(NAME texture_manager_test COMMAND texture_manager_test)

# ============================================================================
# Fase 12: Physics (custom) — raymath header only; no Raylib link
# ============================================================================
artcade_test(physics_test
    physics-test.cpp
    artcade-physics)

# ============================================================================
# Fase 13: LuaHost error-handling — no Raylib, no GameAPI
# ============================================================================
artcade_test(lua_host_test
    lua-host-test.cpp
    artcade-lua-runtime)

# ============================================================================
# Fase 16: Logic Components — 5 Lua components via LuaHost + inline stubs
# Nessuna dipendenza da Raylib o GameAPI (stubs iniettati come Lua puro).
# ============================================================================
artcade_test(logic_components_test
    logic-components-test.cpp
    artcade-lua-runtime)

artcade_test(logic_board_test
    logic-board-test.cpp
    "artcade-logic-core;artcade-logic-runtime;artcade-lua-runtime;artcade-camera-manager")

# ADR-0029: the Code syntax and its parser must stay mutual inverses.
artcade_test(number_expression_syntax_test
    number-expression-syntax-test.cpp
    artcade-logic-core)

# ADR-0037: GameplaySessionSeed is a pure function with no dependency on the
# rest of GameplaySession — compiled directly against its own source instead
# of linking the (Raylib-dependent) artcade-gameplay-session library.
artcade_test(gameplay_session_seed_test
    "gameplay-session-seed-test.cpp;${ARTCADE_RUNTIME_ROOT}/src/app/src/gameplay_session_seed.cpp"
    "")

artcade_test(camera_clamp_test
    camera-clamp-test.cpp
    artcade-world)

# Scene mutation / invalidation (stateless — no Raylib)
artcade_test(scene_invalidation_test
    scene-invalidation-test.cpp
    artcade-scene-system)

artcade_test(scene_mutation_batch_test
    scene-mutation-batch-test.cpp
    artcade-scene-system)

artcade_test(scene_mutation_diff_test
    scene-mutation-diff-test.cpp
    artcade-scene-system)

artcade_test(scene_restart_test
    scene-restart-test.cpp
    "artcade-scene-system;artcade-runtime-entity-gateway;artcade-physics;artcade-variable-manager")

artcade_test(scene_lifecycle_test
    scene-lifecycle-test.cpp
    "artcade-scene-system;artcade-runtime-entity-gateway;artcade-physics")

# ============================================================================
# Fase 17: .artcade ZIP round-trip — richiede artcade-asset-system
# (compilato solo quando Raylib + Lua sono presenti, come asset-system stesso)
# ============================================================================
if(ARTCADE_HAS_RAYLIB AND ARTCADE_HAS_LUA)
    artcade_test(renderer_screen_world_test
        renderer-screen-world-test.cpp
        artcade-renderer)

    add_executable(scene_mutation_geometry_test
        scene-mutation-geometry-test.cpp
    )
    target_include_directories(scene_mutation_geometry_test PRIVATE
        ${ARTCADE_RUNTIME_ROOT}/src
        ${ARTCADE_RUNTIME_ROOT}/src/modules/renderer/include
        ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
    )
    target_link_libraries(scene_mutation_geometry_test PRIVATE
        artcade-renderer
        artcade-scene-system
    )
    add_test(NAME scene_mutation_geometry_test COMMAND scene_mutation_geometry_test)

    artcade_test(presentation_input_builder_test
        presentation-input-builder-test.cpp
        artcade-presentation)

    add_executable(frame_coordinator_test
        frame-coordinator-test.cpp
        ${ARTCADE_RUNTIME_ROOT}/src/app/render/frame_coordinator.cpp
        ${ARTCADE_RUNTIME_ROOT}/src/app/render/scene_frame_builder.cpp
    )
    target_include_directories(frame_coordinator_test PRIVATE
        ${ARTCADE_RUNTIME_ROOT}/src
        ${ARTCADE_RUNTIME_ROOT}/src/app/render
        ${ARTCADE_RUNTIME_ROOT}/src/modules/renderer/include
        ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
    )
    target_link_libraries(frame_coordinator_test PRIVATE
        artcade-renderer
        artcade-presentation
        artcade-scene-system
    )
    add_test(NAME frame_coordinator_test COMMAND frame_coordinator_test)

    add_executable(scene_frame_snapshot_test
        scene-frame-snapshot-test.cpp
        ${ARTCADE_RUNTIME_ROOT}/src/app/render/scene_frame_builder.cpp
    )
    target_include_directories(scene_frame_snapshot_test PRIVATE
        ${ARTCADE_RUNTIME_ROOT}/src
        ${ARTCADE_RUNTIME_ROOT}/src/app/render
        ${ARTCADE_RUNTIME_ROOT}/src/modules/presentation/include
    )
    target_link_libraries(scene_frame_snapshot_test PRIVATE
        artcade-presentation
        artcade-renderer
    )
    add_test(NAME scene_frame_snapshot_test COMMAND scene_frame_snapshot_test)

    artcade_test(scene_gateway_test
        scene-gateway-test.cpp
        "artcade-runtime-entity-gateway;artcade-physics;artcade-variable-manager")

    # Signal/observer contracts of the EnTT-backed gateway: auto indices,
    # lifecycle events, idempotent setIdentity, physics auto-teardown,
    # insertion-order determinism (post-EnTT migration).
    artcade_test(entity_signals_test
        entity-signals-test.cpp
        "artcade-runtime-entity-gateway;artcade-physics")

    artcade_test(entity_collision_query_test
        entity-collision-query-test.cpp
        "artcade-runtime-entity-gateway;artcade-collision")

    artcade_test(collision_world_mask_test
        collision-world-mask-test.cpp
        "artcade-runtime-entity-gateway;artcade-collision;artcade-project-json")

artcade_test(world_tilemap_collision_test
        world-tilemap-collision-test.cpp
        "artcade-world;artcade-scene-system;artcade-physics;artcade-variable-manager")

    artcade_test(world_topdown_boxcollider_test
        world-topdown-boxcollider-test.cpp
        "artcade-world;artcade-scene-system;artcade-physics;artcade-variable-manager")

    artcade_test(world_oneway_landing_test
        world-oneway-landing-test.cpp
        "artcade-world;artcade-scene-system;artcade-runtime-entity-gateway;artcade-physics;artcade-variable-manager")

    artcade_test(world_platformer_side_contact_test
        world-platformer-side-contact-test.cpp
        "artcade-world;artcade-scene-system;artcade-runtime-entity-gateway;artcade-physics;artcade-variable-manager")

    artcade_test(runtime_logic_host_parity_test
        runtime-logic-host-parity-test.cpp
        "artcade-world;artcade-scene-system;artcade-runtime-entity-gateway;artcade-physics;artcade-variable-manager;artcade-sprite-animator")

    # RU-03: gameplay_session.cpp/sprite_frame_resolve.cpp/text_value_formatter.cpp
    # now come from the artcade-gameplay-session library (runtime-cpp/CMakeLists.txt)
    # instead of being re-listed here as loose extra sources.
    artcade_test(gameplay_tick_order_characterization_test
        gameplay-tick-order-characterization-test.cpp
        "artcade-gameplay-session;artcade-world;artcade-scene-system;artcade-runtime-entity-gateway;artcade-physics;artcade-variable-manager;artcade-sprite-animator;artcade-logic-core;artcade-logic-runtime;artcade-script-runtime;artcade-lua-runtime;artcade-game-api;artcade-camera-manager;artcade-event-bus;artcade-game-state;artcade-time;artcade-tween-manager;artcade-audio;artcade-input")
    target_include_directories(gameplay_tick_order_characterization_test PRIVATE
        ${ARTCADE_RUNTIME_ROOT}/libs/lua-5.4/src)
    target_link_libraries(gameplay_tick_order_characterization_test PRIVATE lua54)

    artcade_test(gameplay_session_dependency_guard_test
        gameplay-session-dependency-guard-test.cpp
        "")
    target_compile_definitions(gameplay_session_dependency_guard_test PRIVATE
        ARTCADE_RUNTIME_ROOT="${ARTCADE_RUNTIME_ROOT}")

    artcade_test(game_api_state_test
        game-api-state-test.cpp
        "artcade-game-api;artcade-variable-manager;artcade-camera-manager")
    target_include_directories(game_api_state_test PRIVATE
        ${ARTCADE_RUNTIME_ROOT}/libs/lua-5.4/src)
    target_link_libraries(game_api_state_test PRIVATE lua54)

add_executable(artcade_package_test artcade-package-test.cpp)
target_include_directories(artcade_package_test PRIVATE
    ${ARTCADE_RUNTIME_ROOT}/src
    ${ARTCADE_RUNTIME_ROOT}/src/modules/asset-system/include
    ${ARTCADE_RUNTIME_ROOT}/src/modules/asset-system/src)
target_link_libraries(artcade_package_test PRIVATE artcade-asset-system)
add_test(NAME artcade_package_test COMMAND artcade_package_test)

    # .artcade encryption round-trip — only when Monocypher + key header exist.
    if(ARTCADE_HAS_CRYPTO)
        add_executable(artcade_crypto_test artcade-crypto-test.cpp)
        target_include_directories(artcade_crypto_test PRIVATE
            ${ARTCADE_RUNTIME_ROOT}/src/modules/artcade-archive/include
            ${ARTCADE_RUNTIME_ROOT}/libs/monocypher
            ${ARTCADE_GENERATED_INCLUDE_DIR})
        target_compile_definitions(artcade_crypto_test PRIVATE ARTCADE_HAS_CRYPTO=1)
        target_link_libraries(artcade_crypto_test PRIVATE artcade-archive-core monocypher)
        add_test(NAME artcade_crypto_test COMMAND artcade_crypto_test)

        add_executable(artcade_pack_core_test artcade-pack-core-test.cpp)
        target_include_directories(artcade_pack_core_test PRIVATE
            ${ARTCADE_RUNTIME_ROOT}/src/modules/artcade-pack/include
            ${ARTCADE_RUNTIME_ROOT}/src/modules/artcade-archive/include)
        target_link_libraries(artcade_pack_core_test PRIVATE artcade-pack-core)
        add_test(NAME artcade_pack_core_test COMMAND artcade_pack_core_test)
    endif()

    artcade_test(tilemap_grid_test
        tilemap-grid-test.cpp
        artcade-core)
endif()

# ============================================================================
# Fase 5+: test che richiedono Raylib reale (aggiunti quando ARTCADE_HAS_RAYLIB)
# ============================================================================
# (placeholder per test di renderer, input, audio — verranno aggiunti in Fase 5-8)
