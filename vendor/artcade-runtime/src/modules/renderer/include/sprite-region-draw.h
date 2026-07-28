#pragma once

namespace ArtCade::Modules {

/**
 * One axis-aligned atlas-region submission.
 *
 * destination coordinates are local to the caller-provided draw origin.
 * This renderer contract intentionally carries no tilemap-specific metadata
 * and no Raylib/cache handle.
 */
struct SpriteRegionDraw {
    float srcX = 0.f;
    float srcY = 0.f;
    float srcW = 0.f;
    float srcH = 0.f;
    float dstX = 0.f;
    float dstY = 0.f;
    float dstW = 0.f;
    float dstH = 0.f;
};

} // namespace ArtCade::Modules
