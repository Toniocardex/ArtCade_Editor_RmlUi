add_library(artcade-dialog
    src/dialog-parser.cpp
    src/dialog-manager.cpp
    src/dialog-renderer.cpp
)
target_include_directories(artcade-dialog
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-dialog
    PUBLIC  artcade-core
    PRIVATE artcade-event-bus
            artcade-variable-manager
            artcade-time
)
if(TARGET artcade-renderer)
    target_link_libraries(artcade-dialog PRIVATE artcade-renderer artcade-input)
endif()
