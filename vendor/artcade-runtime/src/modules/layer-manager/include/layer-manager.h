#pragma once

#include "../../../core/module.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * LayerManager — named rendering layers with z-order, visibility and opacity.
 *
 * Layers are sorted by zOrder (ascending → back to front).
 * Each entity is assigned to exactly one layer.
 * The renderer queries sortedLayers() to determine draw order and applies
 * the layer's opacity as an alpha multiplier.
 *
 * Layers are created explicitly via defineLayer(); entities are registered
 * with assignEntity().  A layer can have its opacity tweened smoothly via
 * setOpacity(layer, target, duration).
 */
class LayerManager final : public IModule {
public:
    LayerManager() = default;

    bool init()     override;
    void shutdown() override;

    using EntityId = uint32_t;

    struct LayerInfo {
        std::string name;
        int         zOrder    = 0;
        bool        visible   = true;
        float       opacity   = 1.f;   // 0 = transparent, 1 = opaque

        // Opacity tween state
        float tweenTarget   = 1.f;
        float tweenDuration = 0.f;
        float tweenElapsed  = 0.f;
        float tweenStart    = 1.f;
    };

    // ------------------------------------------------------------------ layer definition

    // Create or replace a layer (idempotent on name)
    void defineLayer(const std::string& name, int zOrder,
                     bool visible = true, float opacity = 1.f);

    bool hasLayer(const std::string& name) const;
    void removeLayer(const std::string& name);

    // ------------------------------------------------------------------ layer control

    void setVisible(const std::string& layer, bool visible);
    void setOpacity(const std::string& layer, float opacity, float duration = 0.f);
    void setZOrder (const std::string& layer, int zOrder);

    bool  isVisible(const std::string& layer) const;
    float opacity  (const std::string& layer) const;
    int   zOrder   (const std::string& layer) const;

    // Call once per frame to advance opacity tweens
    void update(float dt);

    // ------------------------------------------------------------------ entity assignment

    void assignEntity (EntityId entity, const std::string& layer);
    void unassignEntity(EntityId entity);

    // Returns "" if entity has no layer
    const std::string& layerOf(EntityId entity) const;

    // Returns entities assigned to a layer (unordered)
    const std::vector<EntityId>& entitiesInLayer(const std::string& layer) const;

    // ------------------------------------------------------------------ render order

    // Returns layer names sorted back-to-front (lowest zOrder first)
    std::vector<std::string> sortedLayers() const;

    std::size_t layerCount() const;

private:
    std::unordered_map<std::string, LayerInfo>         layers_;
    std::unordered_map<std::string, std::vector<EntityId>> layerEntities_;
    std::unordered_map<EntityId, std::string>          entityLayer_;

    static const std::string kNoLayer;
    static const std::vector<EntityId> kEmptyEntities;
};

} // namespace ArtCade::Modules
