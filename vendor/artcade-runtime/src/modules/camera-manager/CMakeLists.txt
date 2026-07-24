add_library(artcade-camera-manager
    src/camera-manager.cpp
)
target_include_directories(artcade-camera-manager
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-camera-manager
    PUBLIC artcade-core
)
