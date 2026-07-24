#include "sprite-outline-shader.h"

#include <cstdio>

namespace ArtCade::Modules {

namespace {

// Matches raylib default texture vertex shader I/O (rlgl default shader).
static const char kOutlineFragment[] = R"GLSL(
#if defined(GLSL_VERSION)
    #if GLSL_VERSION >= 330
        #version 330
    #else
        #if GLSL_VERSION >= 140
            #version 140
        #else
            #version 100
        #endif
    #endif
#endif

#if (defined(GLSL_VERSION) && GLSL_VERSION >= 330)
    in vec2 fragTexCoord;
    in vec4 fragColor;
    out vec4 finalColor;
    #define SAMPLE(coord) texture(texture0, coord)
    #define OUT_COLOR finalColor
#else
    precision mediump float;
    varying vec2 fragTexCoord;
    varying vec4 fragColor;
    #define SAMPLE(coord) texture2D(texture0, coord)
    #define OUT_COLOR gl_FragColor
#endif

uniform sampler2D texture0;
uniform vec4 outlineColor;
uniform vec2 texelSize;
uniform float outlineSize;

void main()
{
    vec4 texel = SAMPLE(fragTexCoord) * fragColor;
    if (texel.a > 0.15) {
        OUT_COLOR = texel;
        return;
    }

    float neighborAlpha = 0.0;
    for (int ix = -1; ix <= 1; ix++) {
        for (int iy = -1; iy <= 1; iy++) {
            if (ix == 0 && iy == 0) continue;
            vec2 off = vec2(float(ix), float(iy)) * texelSize * outlineSize;
            neighborAlpha = max(neighborAlpha, SAMPLE(fragTexCoord + off).a);
        }
    }

    if (neighborAlpha > 0.15) {
        OUT_COLOR = vec4(outlineColor.rgb, outlineColor.a * neighborAlpha);
    } else {
#if (defined(GLSL_VERSION) && GLSL_VERSION >= 330)
        discard;
#else
        OUT_COLOR = vec4(0.0);
#endif
    }
}
)GLSL";

} // namespace

bool SpriteOutlineShader::load() {
    if (ready) return true;

    shader = LoadShaderFromMemory(nullptr, kOutlineFragment);
    if (shader.id == 0) {
        std::fprintf(stderr, "[renderer] sprite outline shader failed to compile\n");
        return false;
    }

    locOutlineColor = GetShaderLocation(shader, "outlineColor");
    locTexelSize    = GetShaderLocation(shader, "texelSize");
    locOutlineSize  = GetShaderLocation(shader, "outlineSize");
    ready = (locOutlineColor >= 0 && locTexelSize >= 0 && locOutlineSize >= 0);
    if (!ready) {
        std::fprintf(stderr, "[renderer] sprite outline shader missing uniforms\n");
        unload();
    }
    return ready;
}

void SpriteOutlineShader::unload() {
    if (shader.id != 0) {
        UnloadShader(shader);
        shader = {};
    }
    locOutlineColor = locTexelSize = locOutlineSize = -1;
    ready = false;
}

} // namespace ArtCade::Modules
