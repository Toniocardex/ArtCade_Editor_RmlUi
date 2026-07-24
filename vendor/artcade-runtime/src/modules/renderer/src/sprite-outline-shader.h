#pragma once

#include <raylib.h>

namespace ArtCade::Modules {

/** GPU outline for DrawTexturePro (alpha neighborhood). Load after InitWindow. */
struct SpriteOutlineShader {
    Shader shader{};
    int    locOutlineColor = -1;
    int    locTexelSize    = -1;
    int    locOutlineSize  = -1;
    bool   ready           = false;

    bool load();
    void unload();
};

} // namespace ArtCade::Modules
