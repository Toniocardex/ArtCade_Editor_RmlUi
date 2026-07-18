function(artcade_test TARGET SOURCES LIBS)
    add_executable(${TARGET} ${SOURCES})
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${TARGET} PRIVATE ${LIBS})
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endfunction()

# Shared fixtures for the split editor-core suites.
set(ARTCADE_EDITOR_CORE_TEST_HARNESS
    editor_core_test_harness.cpp)

function(artcade_editor_core_suite TARGET SOURCE)
    artcade_test(${TARGET} ${SOURCE} artcade-editor-core)
    target_sources(${TARGET} PRIVATE
        ${ARTCADE_EDITOR_CORE_TEST_HARNESS}
        ${CMAKE_SOURCE_DIR}/src/editor-native/app/generated_sfx_generation_preflight.cpp)
    if(MSVC)
        target_link_options(${TARGET} PRIVATE /STACK:8388608)
    endif()
endfunction()

artcade_editor_core_suite(editor_core_test
    editor-core-test.cpp)

artcade_editor_core_suite(sprite_animation_test
    sprite-animation-test.cpp)

artcade_editor_core_suite(tileset_tilemap_test
    tileset-tilemap-test.cpp)

artcade_editor_core_suite(generated_sfx_model_test
    generated-sfx-model-test.cpp)

artcade_editor_core_suite(script_asset_test
    script-asset-test.cpp)

artcade_test(generated_sfx_editor_controller_test
    generated-sfx-editor-controller-test.cpp
    artcade-editor-core)
target_compile_definitions(generated_sfx_editor_controller_test PRIVATE
    ARTCADE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

artcade_test(generated_sfx_generation_service_test
    generated-sfx-generation-service-test.cpp
    artcade-editor-core)
target_sources(generated_sfx_generation_service_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/editor-native/app/generated_sfx_generation_preflight.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/app/generated_sfx_generation_service.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/app/generated_sfx_output_transaction.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/app/generated_sfx_status_projection.cpp)

artcade_test(logic_board_editor_test
    logic-board-editor-test.cpp
    artcade-editor-core)
target_sources(logic_board_editor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/editor-native/ui/logic_board_editor_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/ui/logic_board_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/ui/ui_markup.cpp)
target_link_libraries(logic_board_editor_test PRIVATE RmlUi::Core)

artcade_test(play_sound_preload_test
    play-sound-preload-test.cpp
    artcade-editor-core)

artcade_test(sfx_synthesizer_test
    ${CMAKE_SOURCE_DIR}/vendor/artcade-runtime/src/modules/sfx/tests/sfx_tests.cpp
    ArtCade::Sfx)
