add_library(artcade-sprite-animation-core STATIC
    src/sprite-animation-core.cpp
)
target_include_directories(artcade-sprite-animation-core PUBLIC include)
target_link_libraries(artcade-sprite-animation-core PUBLIC artcade-core)
