add_library(artcade-layer-manager
    src/layer-manager.cpp
)
target_include_directories(artcade-layer-manager
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-layer-manager
    PUBLIC artcade-core
)
