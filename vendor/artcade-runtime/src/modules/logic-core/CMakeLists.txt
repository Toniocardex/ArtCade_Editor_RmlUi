add_library(artcade-logic-core
    src/logic-core.cpp
    src/logic-board-json.cpp
    src/logic-condition-codegen.cpp
    src/logic-number-expression-json.cpp
    src/logic-number-expression-validation.cpp
    src/logic-number-expression-compiler.cpp
    src/logic-number-expression-format.cpp
    src/logic-number-expression-parse.cpp
)
target_include_directories(artcade-logic-core PUBLIC include)
target_link_libraries(artcade-logic-core PUBLIC artcade-core)

