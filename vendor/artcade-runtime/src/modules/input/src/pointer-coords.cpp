#include "../include/pointer-coords.h"

#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

namespace ArtCade::Modules {

namespace {

std::string g_canvasSelector = "#artcade-canvas";

} // namespace

void pointerCoordsSetCanvasSelector(const char* selector) {
    if (selector && *selector)
        g_canvasSelector = selector;
}

Vec2 pointerCoordsNormalizeToFramebuffer(float x, float y) {
#ifdef __EMSCRIPTEN__
    double cssW = 0.0;
    double cssH = 0.0;
    int    iw   = 0;
    int    ih   = 0;
    emscripten_get_element_css_size(g_canvasSelector.c_str(), &cssW, &cssH);
    emscripten_get_canvas_element_size(g_canvasSelector.c_str(), &iw, &ih);
    const float sx = (cssW > 0.0) ? static_cast<float>(iw / cssW) : 1.f;
    const float sy = (cssH > 0.0) ? static_cast<float>(ih / cssH) : 1.f;
    return { x * sx, y * sy };
#else
    return { x, y };
#endif
}

} // namespace ArtCade::Modules
