add_library(artcade-variable-manager
    src/variable-manager.cpp
)
target_include_directories(artcade-variable-manager
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-variable-manager
    PUBLIC artcade-core
)
