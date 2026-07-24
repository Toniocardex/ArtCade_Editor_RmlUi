#include "../include/raylib_surface.h"

#include <raylib.h>

#include <algorithm>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace {

/**
 * Keeps Module.canvas, its backing store, and the Emscripten offscreen FBO in
 * sync after Raylib SetWindowSize. Without this, -sOFFSCREEN_FRAMEBUFFER=1
 * composites only the initial 1280x720 clear while world draws hit a stale FBO.
 */
EM_JS(void, wasm_sync_offscreen_framebuffer, (int width, int height), {
  var canvas = (typeof Module !== 'undefined' && Module['canvas'])
    ? Module['canvas']
    : null;
  if (!canvas) return;
  if (width > 0 && height > 0) {
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
  }
  try {
    var glHost = (typeof GL !== 'undefined') ? GL
      : ((typeof Module !== 'undefined') ? Module['GL'] : null);
    if (glHost && glHost.resizeOffscreenFramebuffer) {
      glHost.resizeOffscreenFramebuffer(canvas);
    }
  } catch (e) {}
});

} // namespace
#endif

namespace ArtCade::Modules {

bool RaylibSurface::open(uint32_t width,
                         uint32_t height,
                         const std::string& title,
                         uint32_t minViewportWidth,
                         uint32_t minViewportHeight) {
    if (open_) return true;

    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    title_ = title;
    min_viewport_w_ = minViewportWidth;
    min_viewport_h_ = minViewportHeight;

    SetTraceLogLevel(LOG_WARNING);
#ifndef __EMSCRIPTEN__
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#else
    // The editor owns canvas sizing via setWindowSize / editor_set_edit_camera.
    // FLAG_WINDOW_RESIZABLE enables Raylib's EmscriptenResizeCallback, which
    // stretches the framebuffer to window.innerWidth and breaks play preview.
    SetConfigFlags(0);
#endif
    InitWindow(static_cast<int>(width_),
               static_cast<int>(height_),
               title_.c_str());
    open_ = true;

    if (min_viewport_w_ > 0 && min_viewport_h_ > 0) {
        SetWindowMinSize(static_cast<int>(min_viewport_w_),
                         static_cast<int>(min_viewport_h_));
    }
    SetTargetFPS(60);
    sync_size_from_raylib();
#ifdef __EMSCRIPTEN__
    wasm_sync_offscreen_framebuffer(static_cast<int>(width_),
                                    static_cast<int>(height_));
#endif
    return true;
}

void RaylibSurface::close() {
    if (!open_) return;
    CloseWindow();
    open_ = false;
}

void RaylibSurface::set_size(uint32_t width, uint32_t height, const std::string& title) {
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    title_ = title;
    if (!open_) return;
    SetWindowSize(static_cast<int>(width_), static_cast<int>(height_));
    SetWindowTitle(title_.c_str());
    sync_size_from_raylib();
#ifdef __EMSCRIPTEN__
    wasm_sync_offscreen_framebuffer(static_cast<int>(width_),
                                    static_cast<int>(height_));
#endif
}

void RaylibSurface::set_size_for_logical_viewport(uint32_t logicalWidth,
                                                  uint32_t logicalHeight,
                                                  const std::string& title) {
    const uint32_t safeWidth = std::max(1u, logicalWidth);
    const uint32_t safeHeight = std::max(1u, logicalHeight);
    const uint32_t scale = calculate_initial_window_scale(safeWidth, safeHeight);
    set_size(safeWidth * scale, safeHeight * scale, title);
}

void RaylibSurface::set_min_viewport_size(uint32_t width, uint32_t height) {
    min_viewport_w_ = width;
    min_viewport_h_ = height;
    if (!open_ || min_viewport_w_ == 0 || min_viewport_h_ == 0) return;
    SetWindowMinSize(static_cast<int>(min_viewport_w_),
                     static_cast<int>(min_viewport_h_));
}

void RaylibSurface::sync_size_from_raylib() {
    if (!open_) return;
#ifdef __EMSCRIPTEN__
    // The embedded canvas has a runtime framebuffer and a host-controlled CSS box.
    // Keep the authored framebuffer size to avoid applying display scale twice.
    return;
#else
    const int liveW = GetScreenWidth();
    const int liveH = GetScreenHeight();
    if (liveW > 0) width_ = static_cast<uint32_t>(liveW);
    if (liveH > 0) height_ = static_cast<uint32_t>(liveH);
#endif
}

bool RaylibSurface::toggle_borderless_fullscreen() {
#ifndef __EMSCRIPTEN__
    if (!open_) return false;
    ToggleBorderlessWindowed();
    sync_size_from_raylib();
    return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
#else
    return false;
#endif
}

bool RaylibSurface::should_close() const {
    return WindowShouldClose();
}

float RaylibSurface::delta_time() const {
    return GetFrameTime();
}

void RaylibSurface::begin_drawing() {
#ifdef __EMSCRIPTEN__
    // Rebind OFFSCREEN_FRAMEBUFFER every frame so re-parent/resize cannot leave
    // ClearBackground on the visible canvas while world draws hit a stale FBO.
    wasm_sync_offscreen_framebuffer(static_cast<int>(width_),
                                    static_cast<int>(height_));
#endif
    BeginDrawing();
}

void RaylibSurface::end_drawing() {
    EndDrawing();
}

uint32_t RaylibSurface::calculate_initial_window_scale(uint32_t logicalWidth,
                                                       uint32_t logicalHeight) {
    constexpr float kMaxScreenUsage = 0.80f;
    const uint32_t safeWidth = std::max(1u, logicalWidth);
    const uint32_t safeHeight = std::max(1u, logicalHeight);
    int monitor = 0;
    if (IsWindowReady()) monitor = GetCurrentMonitor();
    const int monitorWidth = GetMonitorWidth(monitor);
    const int monitorHeight = GetMonitorHeight(monitor);
    if (monitorWidth <= 0 || monitorHeight <= 0) return 1u;
    const uint32_t availableWidth =
        static_cast<uint32_t>(static_cast<float>(monitorWidth) * kMaxScreenUsage);
    const uint32_t availableHeight =
        static_cast<uint32_t>(static_cast<float>(monitorHeight) * kMaxScreenUsage);
    const uint32_t scaleX = availableWidth / safeWidth;
    const uint32_t scaleY = availableHeight / safeHeight;
    return std::max(1u, std::min(scaleX, scaleY));
}

} // namespace ArtCade::Modules
