add_library(artcade-archive-core STATIC
    src/artcade-crypto.cpp
    src/archive_util.cpp
)
target_include_directories(artcade-archive-core
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-archive-core PUBLIC artcade-core)

if(ARTCADE_HAS_CRYPTO)
    target_link_libraries(artcade-archive-core PRIVATE monocypher)
    target_include_directories(artcade-archive-core PRIVATE ${ARTCADE_GENERATED_INCLUDE_DIR})
    target_compile_definitions(artcade-archive-core PRIVATE ARTCADE_HAS_CRYPTO=1)
endif()
