add_library(artcade-sprite-animator
    src/sprite-animator.cpp
    src/animation-clips-registry.cpp
)
target_include_directories(artcade-sprite-animator
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-sprite-animator
    PUBLIC artcade-core
           artcade-sprite-animation-core
)
