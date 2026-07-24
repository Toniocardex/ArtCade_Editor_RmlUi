add_library(artcade-logic-runtime
    src/logic-runtime.cpp
)
target_include_directories(artcade-logic-runtime PUBLIC include)
target_link_libraries(artcade-logic-runtime
    PUBLIC artcade-core artcade-logic-core
    PRIVATE artcade-lua-runtime lua54)

