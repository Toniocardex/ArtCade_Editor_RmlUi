add_library(artcade-script-runtime
    src/script-runtime.cpp
)
target_include_directories(artcade-script-runtime
    PUBLIC include
    PRIVATE src
)
target_link_libraries(artcade-script-runtime
    PUBLIC artcade-core artcade-script-core
    PRIVATE artcade-lua-runtime
)
