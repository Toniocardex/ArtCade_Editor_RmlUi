#pragma once

#include <cstdint>
#include <string>

namespace ArtCade::Modules {

/**
 * Raylib window / framebuffer lifecycle.
 * Presentation sees only width/height; no placement math here.
 */
class RaylibSurface {
public:
    /**
     * Opens the OS window and configures the framebuffer.
     * @param minViewportWidth minimum window width (logical viewport)
     * @param minViewportHeight minimum window height
     */
    bool open(uint32_t width,
              uint32_t height,
              const std::string& title,
              uint32_t minViewportWidth,
              uint32_t minViewportHeight);

    void close();

    void set_size(uint32_t width, uint32_t height, const std::string& title);

    /**
     * Sizes the native window from a logical viewport using the largest integer
     * scale that fits comfortably on the current monitor.
     */
    void set_size_for_logical_viewport(uint32_t logicalWidth,
                                       uint32_t logicalHeight,
                                       const std::string& title);

    void set_min_viewport_size(uint32_t width, uint32_t height);

    /** Refreshes cached width/height from Raylib (native resize). No-op on WASM. */
    void sync_size_from_raylib();

    /** @return true when borderless fullscreen is active after toggle */
    bool toggle_borderless_fullscreen();

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    const std::string& title() const { return title_; }
    bool is_open() const { return open_; }

    bool should_close() const;
    float delta_time() const;

    void begin_drawing();
    void end_drawing();

private:
    static uint32_t calculate_initial_window_scale(uint32_t logicalWidth,
                                                   uint32_t logicalHeight);

    uint32_t width_ = 1280;
    uint32_t height_ = 720;
    std::string title_ = "ArtCade V2";
    uint32_t min_viewport_w_ = 0;
    uint32_t min_viewport_h_ = 0;
    bool open_ = false;
};

} // namespace ArtCade::Modules
