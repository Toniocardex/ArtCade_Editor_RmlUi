#ifdef __EMSCRIPTEN__

#include "../include/editor-api.h"
#include "editor-spritesheet-preview.h"
#include "../../../modules/renderer/include/renderer.h"
#include "../../../modules/sprite-animator/include/sprite-animator.h"

#include <emscripten.h>
#include <string>
#include <vector>

namespace ArtCade {

namespace {

struct SpritesheetPreviewRequest {
    std::string texturePath;
    std::string clipName;
    float       dtSeconds = 0.f;
    int         canvasW   = 0;
    int         canvasH   = 0;
};

SpritesheetPreviewRequest g_pending;
bool                      g_hasPending = false;
std::string               g_activeClip;

std::vector<unsigned char> g_rgbaScratch;
int                        g_scratchW = 0;
int                        g_scratchH = 0;

void notifySpritesheetPreviewFrame(int status, int w, int h, const unsigned char* rgba) {
    EM_ASM({
        if (typeof window.onSpritesheetPreviewFrame !== 'function') return;
        var status = $3;
        var w = $1;
        var h = $2;
        if (status !== 0 || !$0 || w <= 0 || h <= 0) {
            window.onSpritesheetPreviewFrame(status, w, h, 0);
            return;
        }
        var byteLen = w * h * 4;
        var src = HEAPU8.subarray($0, $0 + byteLen);
        var copy = new Uint8ClampedArray(byteLen);
        copy.set(src);
        window.onSpritesheetPreviewFrame(status, w, h, copy);
    }, rgba, w, h, status);
}

} // namespace

void EditorAPI::queueSpritesheetPreview(
    const char* texturePath,
    const char* clipName,
    float dtSeconds,
    int canvasW,
    int canvasH) {
    if (!texturePath || !*texturePath || !clipName || !*clipName) {
        g_hasPending = false;
        return;
    }
    g_pending.texturePath = texturePath;
    g_pending.clipName    = clipName;
    g_pending.dtSeconds   = dtSeconds > 0.f ? dtSeconds : 0.f;
    g_pending.canvasW     = canvasW > 0 ? canvasW : 64;
    g_pending.canvasH     = canvasH > 0 ? canvasH : 64;
    g_hasPending          = true;
}

void EditorAPI::resetSpritesheetPreview() {
    g_hasPending = false;
    g_activeClip.clear();
    if (s_spriteAnimator)
        s_spriteAnimator->stop(kSpritesheetPreviewEntityId);
}

void EditorAPI::processSpritesheetPreviewQueue() {
    if (!g_hasPending) return;
    g_hasPending = false;

    auto* renderer = s_renderer;
    auto* anim     = s_spriteAnimator;
    if (!renderer || !anim) {
        notifySpritesheetPreviewFrame(-3, 0, 0, nullptr);
        return;
    }
    if (!anim->hasClip(g_pending.clipName)) {
        notifySpritesheetPreviewFrame(-4, 0, 0, nullptr);
        return;
    }

    const int w = g_pending.canvasW;
    const int h = g_pending.canvasH;
    const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (g_scratchW != w || g_scratchH != h || g_rgbaScratch.size() < need) {
        g_rgbaScratch.assign(need, 0);
        g_scratchW = w;
        g_scratchH = h;
    }

    if (g_activeClip != g_pending.clipName) {
        anim->play(kSpritesheetPreviewEntityId, g_pending.clipName);
        g_activeClip = g_pending.clipName;
    }
    if (g_pending.dtSeconds > 0.f)
        anim->update(g_pending.dtSeconds);

    const auto fr = anim->currentFrame(kSpritesheetPreviewEntityId);
    if (fr.w <= 0 || fr.h <= 0) {
        notifySpritesheetPreviewFrame(-7, w, h, nullptr);
        return;
    }

    const int code = renderer->captureSpriteRegionFrame(
        g_pending.texturePath,
        static_cast<float>(fr.x), static_cast<float>(fr.y),
        static_cast<float>(fr.w), static_cast<float>(fr.h),
        w, h, g_rgbaScratch.data(), static_cast<int>(need));

    notifySpritesheetPreviewFrame(code, w, h, g_rgbaScratch.data());
}

} // namespace ArtCade

#endif
