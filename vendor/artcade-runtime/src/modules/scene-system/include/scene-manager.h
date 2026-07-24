#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include <vector>
#include <utility>

namespace ArtCade::Modules {

/**
 * SceneManager - scene registry and active-scene selection.
 *
 * Entity storage lives in RuntimeEntityGateway / EntityRegistry. Loading a
 * scene only changes the active scene id; RuntimeEntityGateway applies
 * activation/deactivation by walking the active scene's entityIds.
 */
class SceneManager final : public IModule {
public:
    SceneManager() = default;

    bool init() override;
    void shutdown() override;

    void registerScenes(const std::unordered_map<SceneId, SceneDef>& scenes,
                        const std::unordered_map<EntityId, EntityDef>& entityDefs);

    bool loadScene(const SceneId& id);

    SceneId           activeSceneId()     const { return activeId_; }
    const SceneDef*   activeScene()       const;
    const SceneDef*   getScene(const SceneId& id) const;
    SceneDef*         getSceneMutable(const SceneId& id);
    SceneDef*         activeSceneMutable();
    void              removeEntityFromAllScenes(EntityId id);
    /** Merge an entity def into the project snapshot (editor incremental sync). */
    void              upsertEntityDef(EntityId id, const EntityDef& def);
    const EntityDef*  getEntityDef(EntityId id) const;

    // Phase F3: project-level tileset assets (spritesheets). Set at startup
    // from the ProjectDoc and refreshed on editor hot-reload so the render
    // path always sees the current tilesets.
    void setTilesets(std::vector<TilesetAsset> tilesets) {
        tilesets_ = std::move(tilesets);
    }
    const std::vector<TilesetAsset>& tilesets() const { return tilesets_; }

    /** Project render stack (index 0 = highest priority / drawn last). */
    void setSceneLayers(std::vector<SceneLayerDef> layers) {
        sceneLayers_ = std::move(layers);
    }
    const std::vector<SceneLayerDef>& sceneLayers() const { return sceneLayers_; }

    /** Render rank for @p layerId (count - index; higher = drawn later). 0 if unknown. */
    int layerRank(const std::string& layerId) const;
    /** Per-scene visual settings for @p layerId in the active scene (defaults when absent). */
    SceneLayerSettings activeLayerSettings(const std::string& layerId) const;
    /** Global editor lock state for @p layerId. */
    bool layerLocked(const std::string& layerId) const;

private:
    std::unordered_map<SceneId, SceneDef>        scenes_;
    std::unordered_map<EntityId, EntityDef>      entityDefs_;
    SceneId                                      activeId_;
    std::vector<TilesetAsset>                    tilesets_;      // Phase F3
    std::vector<SceneLayerDef>                   sceneLayers_;
};

} // namespace ArtCade::Modules
