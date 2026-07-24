cmake_minimum_required(VERSION 3.20)

# ============================================================================
# artcade-collision — header-only shared collision kernel (World + Physics)
# ============================================================================
add_library(artcade-collision INTERFACE)

target_include_directories(artcade-collision
    INTERFACE include
)

target_link_libraries(artcade-collision
    INTERFACE artcade-core
)
