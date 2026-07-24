#pragma once

#include "../../../core/types.h"

namespace ArtCade::Modules {

/**
 * Normalized runtime patch for one scene (editor projection or gameplay).
 * Only fields marked with has* are applied.
 */
struct ScenePatch {
    Vec2 worldSize{};
    Vec2 viewportSize{};
    Vec4 backgroundColor{};
    std::string name;
    std::unordered_map<std::string, SceneLayerSettings> layerSettings;

    bool hasWorldSize    = false;
    bool hasViewportSize = false;
    bool hasBackground   = false;
    bool hasName         = false;
    bool hasLayerSettings = false;

    /**
     * Builds a patch from a SceneDef subset (e.g. editor_set_scene_settings JSON).
     * Positive world/viewport sizes are treated as explicit geometry fields.
     */
    static ScenePatch from_projection(const SceneDef& def);
};

} // namespace ArtCade::Modules
