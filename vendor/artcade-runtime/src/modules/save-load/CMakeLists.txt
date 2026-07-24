add_library(artcade-save-load
    src/save-load-manager.cpp
)
target_include_directories(artcade-save-load
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-save-load
    PUBLIC
        artcade-core
        artcade-variable-manager
)
