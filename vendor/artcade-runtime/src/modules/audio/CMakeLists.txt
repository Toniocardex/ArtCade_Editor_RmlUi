add_library(artcade-audio
    src/audio.cpp
)
target_include_directories(artcade-audio
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-audio
    PUBLIC  artcade-core
    PRIVATE raylib
)
