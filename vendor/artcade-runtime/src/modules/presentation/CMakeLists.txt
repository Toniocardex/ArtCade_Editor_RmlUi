add_library(artcade-presentation
    src/output_policy.cpp
    src/surface_metrics.cpp
    src/coordinate_mapper.cpp
    src/view_controller.cpp
    src/camera_compose.cpp
    src/presentation_snapshot.cpp
    src/presentation_solver.cpp
    src/presentation_system.cpp
    src/presentation_bindings.cpp
    src/presentation_snapshot_wasm.cpp
    src/editor_viewport_controller.cpp
    src/frame_selection.cpp
    src/presentation_state_sync.cpp
    src/presentation_input_builder.cpp
    src/editor_viewport_navigation.cpp
    src/editor_viewport_service.cpp
    src/editor_zoom_policy.cpp
)
target_include_directories(artcade-presentation
    PUBLIC include
    PRIVATE src
)
target_link_libraries(artcade-presentation
    PUBLIC artcade-core
)
