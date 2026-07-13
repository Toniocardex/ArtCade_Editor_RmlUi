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
