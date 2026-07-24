add_library(artcade-event-bus
    src/event-bus.cpp
)
target_include_directories(artcade-event-bus
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-event-bus
    PUBLIC artcade-core
)
