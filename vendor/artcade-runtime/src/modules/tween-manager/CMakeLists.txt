add_library(artcade-tween-manager
    src/tween-manager.cpp
)
target_include_directories(artcade-tween-manager
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-tween-manager
    PUBLIC artcade-core
)
