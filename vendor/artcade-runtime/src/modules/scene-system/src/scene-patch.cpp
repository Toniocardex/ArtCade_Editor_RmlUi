#include "../include/scene-patch.h"

namespace ArtCade::Modules {

ScenePatch ScenePatch::from_projection(const SceneDef& def) {
    ScenePatch patch;
    if (def.worldSize.x > 0.f && def.worldSize.y > 0.f) {
        patch.worldSize = def.worldSize;
        patch.hasWorldSize = true;
    }
    if (def.viewportSize.x > 0.f && def.viewportSize.y > 0.f) {
        patch.viewportSize = def.viewportSize;
        patch.hasViewportSize = true;
    }
    patch.backgroundColor = def.backgroundColor;
    patch.hasBackground = true;
    if (!def.name.empty()) {
        patch.name = def.name;
        patch.hasName = true;
    }
    patch.layerSettings = def.layerSettings;
    patch.hasLayerSettings = true;
    return patch;
}

} // namespace ArtCade::Modules
