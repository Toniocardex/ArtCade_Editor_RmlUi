add_library(artcade-pack-core STATIC
    src/project_packer.cpp
)
target_include_directories(artcade-pack-core
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-pack-core PUBLIC artcade-archive-core artcade-core)
