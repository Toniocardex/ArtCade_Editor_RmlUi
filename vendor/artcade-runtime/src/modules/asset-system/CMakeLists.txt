add_library(artcade-asset-system
    src/asset-loader.cpp
    src/asset-manifest-index.cpp
    src/zip-reader.cpp        # Phase 17: ZIP reader for .artcade archives
)
target_include_directories(artcade-asset-system
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-asset-system
    PUBLIC  artcade-core artcade-object-type-materialize artcade-project-json
            artcade-logic-core artcade-archive-core
)

# When raylib is present, expose raylib/src for sinfl.h (DEFLATE decompressor)
# and link raylib so sinflate() (compiled in rcore.c) is available at link time.
if(TARGET raylib)
    target_include_directories(artcade-asset-system PRIVATE
        ${ARTCADE_RUNTIME_ROOT}/libs/raylib/src      # → #include "external/sinfl.h"
    )
    target_link_libraries(artcade-asset-system PRIVATE raylib)
endif()
