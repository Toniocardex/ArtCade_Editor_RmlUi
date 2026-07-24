#pragma once

#include <cstdint>
#include <functional>

namespace ArtCade::Modules {

/**
 * Off-screen readback for editor previews (sprite region capture).
 * Isolated from the normal frame lifecycle.
 */
class CaptureService {
public:
    CaptureService();
    ~CaptureService();

    CaptureService(const CaptureService&) = delete;
    CaptureService& operator=(const CaptureService&) = delete;

    void shutdown();

    /**
     * Rasterizes a region into @p rgbaOut (RGBA8, top-left origin).
     * @param draw_region invoked inside the preview RT to paint the region
     * @param srcW source region width in pixels (must be > 0)
     * @param srcH source region height in pixels (must be > 0)
     * @return 0 on success, negative on failure
     */
    int capture_sprite_region(
        const std::function<bool(float dstX, float dstY,
                                 float dstW, float dstH)>& draw_region,
        float srcW,
        float srcH,
        int canvasW,
        int canvasH,
        unsigned char* rgbaOut,
        int rgbaOutLen);

private:
    struct Impl;
    struct Impl* impl_ = nullptr;
};

} // namespace ArtCade::Modules
