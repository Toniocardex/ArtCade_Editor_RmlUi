add_library(artcade-renderer
    src/renderer.cpp
    src/renderer_frame.cpp
    src/renderer_draw.cpp
    src/renderer_camera.cpp
    src/raylib_surface.cpp
    src/render_resources.cpp
    src/capture_service.cpp
    src/render_pipeline.cpp
    src/passes/blit_pass.cpp
    src/sprite-outline-shader.cpp
    src/texture-cache.cpp
    src/font-cache.cpp
)
target_include_directories(artcade-renderer
    PUBLIC  include
    PRIVATE src
)
target_link_libraries(artcade-renderer
    PUBLIC  artcade-core artcade-presentation
    PRIVATE raylib
)
