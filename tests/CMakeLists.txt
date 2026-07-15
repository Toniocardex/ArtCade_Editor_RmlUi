function(artcade_test TARGET SOURCES LIBS)
    add_executable(${TARGET} ${SOURCES})
    target_include_directories(${TARGET} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${TARGET} PRIVATE ${LIBS})
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endfunction()

artcade_test(editor_core_test
    editor-core-test.cpp
    artcade-editor-core)
if(MSVC)
    target_link_options(editor_core_test PRIVATE /STACK:8388608)
endif()

artcade_test(logic_board_editor_test
    logic-board-editor-test.cpp
    artcade-editor-core)
target_sources(logic_board_editor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/editor-native/ui/logic_board_editor_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/ui/logic_board_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/editor-native/ui/ui_markup.cpp)
target_link_libraries(logic_board_editor_test PRIVATE RmlUi::Core)

artcade_test(sfx_synthesizer_test
    ${CMAKE_SOURCE_DIR}/vendor/artcade-runtime/src/modules/sfx/tests/sfx_tests.cpp
    ArtCade::Sfx)
