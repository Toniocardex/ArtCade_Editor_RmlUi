cmake_minimum_required(VERSION 3.20)

# ============================================================================
# artcade-physics — custom solver (collision_math + raymath); no Box2D
# ============================================================================
set(_RAYMATH_DIR ${ARTCADE_RUNTIME_ROOT}/libs/raylib/src)
if(NOT EXISTS ${_RAYMATH_DIR}/raymath.h)
    message(FATAL_ERROR "[artcade-physics] raymath.h not found at ${_RAYMATH_DIR}")
endif()

add_library(artcade-physics
    src/physics.cpp
)

target_include_directories(artcade-physics
    PUBLIC  include
    PRIVATE src
            ${_RAYMATH_DIR}
)

target_link_libraries(artcade-physics
    PUBLIC artcade-core
           artcade-collision
)
