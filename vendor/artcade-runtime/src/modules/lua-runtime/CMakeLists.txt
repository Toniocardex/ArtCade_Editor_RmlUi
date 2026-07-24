add_library(artcade-lua-runtime
    src/lua-host.cpp
)
target_include_directories(artcade-lua-runtime
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-lua-runtime
    PUBLIC  artcade-core
    PRIVATE lua54 artcade-logic-core artcade-script-core
)
