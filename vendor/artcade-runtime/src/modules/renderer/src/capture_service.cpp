#include "../include/capture_service.h"

#include <raylib.h>

#include <cstring>

namespace ArtCade::Modules {

struct CaptureService::Impl {
    RenderTexture2D rt{};
    int w = 0;
    int h = 0;
};

CaptureService::CaptureService() : impl_(new Impl()) {}

CaptureService::~CaptureService() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

void CaptureService::shutdown() {
    if (!impl_ || impl_->rt.id == 0) return;
    UnloadRenderTexture(impl_->rt);
    impl_->rt = {};
    impl_->w = 0;
    impl_->h = 0;
}

namespace {

bool ensure_preview_target(RenderTexture2D& rt, int& w, int& h, int newW, int newH) {
    if (newW <= 0 || newH <= 0) return false;
    if (rt.id != 0 && w == newW && h == newH) return true;
    if (rt.id != 0) UnloadRenderTexture(rt);
    rt = LoadRenderTexture(newW, newH);
    w = newW;
    h = newH;
    return rt.id != 0;
}

} // namespace

int CaptureService::capture_sprite_region(
    const std::function<bool(float dstX, float dstY,
                             float dstW, float dstH)>& draw_region,
    float srcW,
    float srcH,
    int canvasW,
    int canvasH,
    unsigned char* rgbaOut,
    int rgbaOutLen) {
    if (!impl_ || !rgbaOut || rgbaOutLen <= 0) return -1;
    const int w = canvasW > 0 ? canvasW : 64;
    const int h = canvasH > 0 ? canvasH : 64;
    const int need = w * h * 4;
    if (rgbaOutLen < need || srcW <= 0.f || srcH <= 0.f) return -2;
    if (!ensure_preview_target(impl_->rt, impl_->w, impl_->h, w, h)) return -3;

    BeginTextureMode(impl_->rt);
    ClearBackground(BLANK);
    constexpr float kPreviewPad = 8.f;
    if (!draw_region || !draw_region(kPreviewPad, kPreviewPad, srcW, srcH)) {
        EndTextureMode();
        return -4;
    }
    EndTextureMode();

    Image pixels = LoadImageFromTexture(impl_->rt.texture);
    if (!pixels.data || pixels.width != w || pixels.height != h) {
        if (pixels.data) UnloadImage(pixels);
        return -5;
    }
    ImageFlipVertical(&pixels);
    std::memcpy(rgbaOut, pixels.data, static_cast<size_t>(need));
    UnloadImage(pixels);
    return 0;
}

} // namespace ArtCade::Modules
