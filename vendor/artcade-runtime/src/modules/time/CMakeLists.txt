add_library(artcade-time
    src/time-manager.cpp
)
target_include_directories(artcade-time
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-time
    PUBLIC artcade-core
)
