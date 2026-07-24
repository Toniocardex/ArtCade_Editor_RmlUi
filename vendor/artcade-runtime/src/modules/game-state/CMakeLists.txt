add_library(artcade-game-state
    src/game-state-manager.cpp
    src/splash-state.cpp
)
target_include_directories(artcade-game-state
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-game-state
    PUBLIC
        artcade-core
        artcade-event-bus
        raylib
)
