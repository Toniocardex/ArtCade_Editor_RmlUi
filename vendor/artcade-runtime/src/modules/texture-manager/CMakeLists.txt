add_library(artcade-texture-manager
    src/texture-manager.cpp
)
target_include_directories(artcade-texture-manager
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-texture-manager
    PUBLIC
        artcade-core
        raylib
)
