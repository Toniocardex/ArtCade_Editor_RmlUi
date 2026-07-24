add_library(artcade-logic-core
    src/logic-core.cpp
    src/logic-board-json.cpp
    src/logic-condition-codegen.cpp
)
target_include_directories(artcade-logic-core PUBLIC include)
target_link_libraries(artcade-logic-core PUBLIC artcade-core)

