// scene-manager.cpp - scene registry + active-scene selection
#include "../include/scene-manager.h"

#include <algorithm>

namespace ArtCade::Modules {

bool SceneManager::init()     { return true; }
void SceneManager::shutdown() { scenes_.clear(); entityDefs_.clear(); }

void SceneManager::registerScenes(
    const std::unordered_map<SceneId, SceneDef>& scenes,
    const std::unordered_map<EntityId, EntityDef>& defs)
{
    scenes_     = scenes;
    entityDefs_ = defs;
}

bool SceneManager::loadScene(const SceneId& id) {
    auto it = scenes_.find(id);
    if (it == scenes_.end()) return false;
    activeId_ = id;
    return true;
}

const SceneDef* SceneManager::activeScene() const {
    return getScene(activeId_);
}

const SceneDef* SceneManager::getScene(const SceneId& id) const {
    auto it = scenes_.find(id);
    return (it != scenes_.end()) ? &it->second : nullptr;
}

SceneDef* SceneManager::getSceneMutable(const SceneId& id) {
    auto it = scenes_.find(id);
    return (it != scenes_.end()) ? &it->second : nullptr;
}

SceneDef* SceneManager::activeSceneMutable() {
    return getSceneMutable(activeId_);
}

void SceneManager::upsertEntityDef(EntityId id, const EntityDef& def) {
    entityDefs_[id] = def;
}

const EntityDef* SceneManager::getEntityDef(EntityId id) const {
    auto it = entityDefs_.find(id);
    return (it != entityDefs_.end()) ? &it->second : nullptr;
}

int SceneManager::layerRank(const std::string& layerId) const {
    const int count = static_cast<int>(sceneLayers_.size());
    for (int i = 0; i < count; ++i) {
        if (sceneLayers_[static_cast<size_t>(i)].id == layerId)
            return count - i;
    }
    return 0;
}

SceneLayerSettings SceneManager::activeLayerSettings(const std::string& layerId) const {
    const SceneDef* scene = activeScene();
    if (scene) {
        const auto it = scene->layerSettings.find(layerId);
        if (it != scene->layerSettings.end())
            return it->second;
    }
    return SceneLayerSettings{};  // visible, opacity 1, parallax 1, no background
}

bool SceneManager::layerLocked(const std::string& layerId) const {
    for (const auto& layer : sceneLayers_) {
        if (layer.id == layerId)
            return layer.locked;
    }
    return false;
}

void SceneManager::removeEntityFromAllScenes(EntityId id) {
    for (auto& [sceneId, scene] : scenes_) {
        (void)sceneId;
        auto& ids = scene.entityIds;
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    }
}

} // namespace ArtCade::Modules
