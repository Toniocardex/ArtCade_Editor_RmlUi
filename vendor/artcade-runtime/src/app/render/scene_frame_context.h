#pragma once

#include "../../core/types.h"
#include "editor-overlay-renderer.h"
#include "scene_frame_snapshot.h"

#include <unordered_map>
#include <vector>

namespace ArtCade {

namespace Modules {
class Renderer;
class SpriteAnimator;
class RuntimeEntityGateway;
class SceneManager;
}

struct SceneFrameContext {
    /** Authoritative immutable frame snapshot (PR5). Non-null during render
     * passes. RU-02g: also carries the resolved `renderables`/`elapsedTime`
     * gameplay data (GameplaySession::buildFrameSnapshot()), which is why
     * VariableManager/TimeManager are no longer members here - the entity
     * draw pass and background pass read those fields off the snapshot
     * instead of querying the live modules during draw. */
    const SceneFrameSnapshot* frameSnapshot = nullptr;

    Modules::Renderer* renderer = nullptr;
    // RU-02g: still queried live, but only by editor-authoring overlays
    // (gizmo_pass - selection/hidden-in-game outlines), not by the gameplay
    // entity draw pass - out of the RU-02g gate's scope ("handle authoring").
    Modules::SpriteAnimator* spriteAnimator = nullptr;
    Modules::RuntimeEntityGateway* entityGateway = nullptr;
    // RU-02g: sceneLayers()/tilesets() are authoring-adjacent data (same
    // aliasing precedent as frameSnapshot->tilemap/tilemapLayers below),
    // still read live by scene_background_pass.
    Modules::SceneManager* sceneManager = nullptr;
    std::vector<EntityId>* selectedEntityIds = nullptr;
    const std::unordered_map<std::string, TilesetAsset>* tilesets = nullptr;
    const std::unordered_map<int, Vec4>* tileColors = nullptr;
};

} // namespace ArtCade
