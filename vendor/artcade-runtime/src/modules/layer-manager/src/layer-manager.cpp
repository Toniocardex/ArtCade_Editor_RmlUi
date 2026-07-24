#include "../include/layer-manager.h"
#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

const std::string          LayerManager::kNoLayer      = "";
const std::vector<LayerManager::EntityId> LayerManager::kEmptyEntities = {};

bool LayerManager::init() {
    layers_.clear();
    layerEntities_.clear();
    entityLayer_.clear();
    return true;
}

void LayerManager::shutdown() {
    layers_.clear();
    layerEntities_.clear();
    entityLayer_.clear();
}

// ------------------------------------------------------------------ layer definition

void LayerManager::defineLayer(const std::string& name, int zOrder,
                                bool visible, float opacity) {
    LayerInfo& info = layers_[name];
    info.name       = name;
    info.zOrder     = zOrder;
    info.visible    = visible;
    info.opacity    = opacity;
    info.tweenTarget   = opacity;
    info.tweenDuration = 0.f;
    // Ensure entity bucket exists
    if (layerEntities_.find(name) == layerEntities_.end())
        layerEntities_[name] = {};
}

bool LayerManager::hasLayer(const std::string& name) const {
    return layers_.count(name) > 0;
}

void LayerManager::removeLayer(const std::string& name) {
    // Unassign all entities in this layer
    auto it = layerEntities_.find(name);
    if (it != layerEntities_.end()) {
        for (EntityId e : it->second)
            entityLayer_.erase(e);
        layerEntities_.erase(it);
    }
    layers_.erase(name);
}

// ------------------------------------------------------------------ layer control

void LayerManager::setVisible(const std::string& layer, bool visible) {
    auto it = layers_.find(layer);
    if (it != layers_.end()) it->second.visible = visible;
}

void LayerManager::setOpacity(const std::string& layer, float opacity, float duration) {
    auto it = layers_.find(layer);
    if (it == layers_.end()) return;
    LayerInfo& l = it->second;
    if (duration <= 0.f) {
        l.opacity = l.tweenTarget = opacity;
        l.tweenDuration = 0.f;
    } else {
        l.tweenStart    = l.opacity;
        l.tweenTarget   = opacity;
        l.tweenDuration = duration;
        l.tweenElapsed  = 0.f;
    }
}

void LayerManager::setZOrder(const std::string& layer, int z) {
    auto it = layers_.find(layer);
    if (it != layers_.end()) it->second.zOrder = z;
}

bool LayerManager::isVisible(const std::string& layer) const {
    auto it = layers_.find(layer);
    return (it != layers_.end()) && it->second.visible;
}

float LayerManager::opacity(const std::string& layer) const {
    auto it = layers_.find(layer);
    return (it != layers_.end()) ? it->second.opacity : 1.f;
}

int LayerManager::zOrder(const std::string& layer) const {
    auto it = layers_.find(layer);
    return (it != layers_.end()) ? it->second.zOrder : 0;
}

void LayerManager::update(float dt) {
    for (auto& [name, l] : layers_) {
        if (l.tweenDuration <= 0.f) continue;
        l.tweenElapsed += dt;
        float t = std::min(l.tweenElapsed / l.tweenDuration, 1.f);
        l.opacity = l.tweenStart + (l.tweenTarget - l.tweenStart) * t;
        if (t >= 1.f) {
            l.opacity       = l.tweenTarget;
            l.tweenDuration = 0.f;
        }
    }
}

// ------------------------------------------------------------------ entity assignment

void LayerManager::assignEntity(EntityId entity, const std::string& layer) {
    // Remove from old layer if any
    auto eit = entityLayer_.find(entity);
    if (eit != entityLayer_.end()) {
        auto& vec = layerEntities_[eit->second];
        vec.erase(std::remove(vec.begin(), vec.end(), entity), vec.end());
    }
    entityLayer_[entity] = layer;
    layerEntities_[layer].push_back(entity);
}

void LayerManager::unassignEntity(EntityId entity) {
    auto eit = entityLayer_.find(entity);
    if (eit == entityLayer_.end()) return;
    auto& vec = layerEntities_[eit->second];
    vec.erase(std::remove(vec.begin(), vec.end(), entity), vec.end());
    entityLayer_.erase(eit);
}

const std::string& LayerManager::layerOf(EntityId entity) const {
    auto it = entityLayer_.find(entity);
    return (it != entityLayer_.end()) ? it->second : kNoLayer;
}

const std::vector<LayerManager::EntityId>&
LayerManager::entitiesInLayer(const std::string& layer) const {
    auto it = layerEntities_.find(layer);
    return (it != layerEntities_.end()) ? it->second : kEmptyEntities;
}

// ------------------------------------------------------------------ render order

std::vector<std::string> LayerManager::sortedLayers() const {
    std::vector<std::pair<int, std::string>> order;
    order.reserve(layers_.size());
    for (const auto& [name, info] : layers_)
        order.push_back({ info.zOrder, name });
    std::sort(order.begin(), order.end());

    std::vector<std::string> result;
    result.reserve(order.size());
    for (auto& [z, name] : order)
        result.push_back(name);
    return result;
}

std::size_t LayerManager::layerCount() const {
    return layers_.size();
}

} // namespace ArtCade::Modules
