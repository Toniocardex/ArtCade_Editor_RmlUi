# ----------------------------------------------------------------------------
# EnTT — header-only ECS, backs EntityRegistry (internal storage of the
# RuntimeEntityGateway). Public gateway API stays unchanged; EnTT is a
# PRIVATE link dependency so consumers don't get entt::registry symbols
# leaking into their TU.
# ----------------------------------------------------------------------------
include(FetchContent)
set(ENTT_INSTALL          OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTING    OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_LIB        OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_EXAMPLE    OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TOOLS      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(EnTT)

add_library(artcade-runtime-entity-gateway
    src/runtime-entity-gateway.cpp
    src/entity-registry.cpp
    src/physics-body-rules.cpp
    src/collision-profile-resolve.cpp
)
target_include_directories(artcade-runtime-entity-gateway
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-runtime-entity-gateway
    PUBLIC
        artcade-core
        artcade-object-type-materialize
        artcade-scene-system
        artcade-physics
        artcade-sprite-animator
    PRIVATE
        EnTT::EnTT
)
