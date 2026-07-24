#pragma once

#include "collision_math.h"

#include "../../../core/types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ArtCade::CollisionWorld {

struct ShapeRef {
    EntityId id = INVALID_ENTITY;
    size_t   shapeIndex = 0;
    BodyType bodyType = BodyType::Static;
    CollisionShape shape;
    uint32_t layerBits = 0;
    uint32_t maskBits = 0;
    PhysicsMath::ShapeInstance instance;
    PhysicsMath::Aabb aabb;
};

struct RaycastResult {
    bool     hit = false;
    EntityId entityId = INVALID_ENTITY;
    size_t   shapeIndex = 0;
    Vec2     point{};
    float    distance = 0.f;
};

struct Filter {
    std::string layerId;
    std::string role;
    std::string response;
    std::string className;
    std::string tag;
};

struct ContactEvent {
    enum class Kind { Enter, Stay, Exit };
    Kind     kind = Kind::Enter;
    EntityId self = INVALID_ENTITY;
    EntityId other = INVALID_ENTITY;
    std::string selfRole;
    std::string otherRole;
    std::string selfResponse;
    std::string otherResponse;
    std::string selfLayerId;
    std::string otherLayerId;
    Vec2 normal{};
    Vec2 point{};
};

struct ShapePairKey {
    uint64_t a = 0;
    uint64_t b = 0;

    bool operator==(const ShapePairKey& other) const {
        return a == other.a && b == other.b;
    }
};

struct ShapePairKeyHash {
    size_t operator()(const ShapePairKey& key) const noexcept {
        const uint64_t mixed = key.a ^ (key.b + 0x9e3779b97f4a7c15ull
            + (key.a << 6) + (key.a >> 2));
        return static_cast<size_t>(mixed ^ (mixed >> 32));
    }
};

struct SpatialCell {
    int x = 0;
    int y = 0;

    bool operator==(const SpatialCell& other) const {
        return x == other.x && y == other.y;
    }
};

struct SpatialCellHash {
    size_t operator()(const SpatialCell& cell) const noexcept {
        const uint64_t ux = static_cast<uint32_t>(cell.x);
        const uint64_t uy = static_cast<uint32_t>(cell.y);
        const uint64_t mixed = ux ^ (uy + 0x9e3779b97f4a7c15ull
            + (ux << 6) + (ux >> 2));
        return static_cast<size_t>(mixed ^ (mixed >> 32));
    }
};

struct LayerTable {
    std::unordered_map<std::string, uint32_t> bitsById;

    uint32_t bitFor(const std::string& id) const {
        auto it = bitsById.find(id);
        if (it == bitsById.end()) return 1u;
        return it->second;
    }
};

inline std::string roleName(CollisionShapeRole role) {
    switch (role) {
    case CollisionShapeRole::Feet: return "feet";
    case CollisionShapeRole::Hurtbox: return "hurtbox";
    case CollisionShapeRole::Hitbox: return "hitbox";
    case CollisionShapeRole::Interaction: return "interaction";
    case CollisionShapeRole::Body:
    default: return "body";
    }
}

inline std::string responseName(CollisionResponse response) {
    return response == CollisionResponse::Sensor ? "sensor" : "solid";
}

inline LayerTable makeLayerTable(const std::vector<PhysicsLayerDef>& layers) {
    LayerTable table;
    for (const auto& layer : layers) {
        if (!layer.id.empty() && layer.bit < 32)
            table.bitsById[layer.id] = 1u << layer.bit;
    }
    if (table.bitsById.empty())
        table.bitsById["default"] = 1u;
    return table;
}

inline bool canCollide(const ShapeRef& a, const ShapeRef& b) {
    return (a.layerBits & b.maskBits) != 0
        && (b.layerBits & a.maskBits) != 0;
}

inline bool matchesFilter(const ShapeRef& shape, const Filter& filter) {
    if (!filter.layerId.empty() && shape.shape.layerId != filter.layerId)
        return false;
    if (!filter.role.empty() && roleName(shape.shape.role) != filter.role)
        return false;
    if (!filter.response.empty() && responseName(shape.shape.response) != filter.response)
        return false;
    return true;
}

inline PhysicsMath::ShapeInstance shapeInstance(
    const Transform& transform,
    const CollisionShape& shape)
{
    // Local shape offset/size are in unscaled object space; world extents
    // follow Transform.scale magnitude (flip is separate from scale).
    const float sx = std::fabs(transform.scale.x);
    const float sy = std::fabs(transform.scale.y);
    PhysicsMath::ShapeInstance inst;
    inst.position = transform.position;
    inst.offset = { shape.offset.x * sx, shape.offset.y * sy };
    inst.size = { shape.size.x * sx, shape.size.y * sy };
    if (shape.type == CollisionShapeType::Circle) {
        inst.shape = ColliderShape::Circle;
        const float r = std::max(0.5f, shape.radius * std::max(sx, sy));
        inst.size = { r, r };
    } else if (shape.type == CollisionShapeType::Capsule) {
        inst.shape = ColliderShape::Capsule;
        inst.size = {
            std::max(1.f, shape.size.x * sx),
            std::max(1.f, shape.size.y * sy),
        };
    } else if (shape.type == CollisionShapeType::Polygon && shape.points.size() >= 3) {
        inst.shape = ColliderShape::Polygon;
        inst.points.reserve(shape.points.size());
        for (const Vec2& p : shape.points)
            inst.points.push_back({ p.x * sx, p.y * sy });
    } else {
        inst.shape = ColliderShape::Rectangle;
    }
    return inst;
}

class World {
public:
    void setLayers(const std::vector<PhysicsLayerDef>& layers) {
        layers_ = makeLayerTable(layers);
    }

    void clear() {
        shapes_.clear();
        entityRanges_.clear();
        spatialCells_.clear();
    }

    void addEntity(EntityId id,
                   const Transform& transform,
                   const CollisionBodyComponent& body)
    {
        if (!body.enabled) return;
        const size_t start = shapes_.size();
        for (size_t i = 0; i < body.shapes.size(); ++i) {
            const CollisionShape& shape = body.shapes[i];
            if (!shape.enabled) continue;
            ShapeRef ref;
            ref.id = id;
            ref.shapeIndex = i;
            ref.bodyType = body.bodyType;
            ref.shape = shape;
            ref.layerBits = layers_.bitFor(shape.layerId);
            ref.maskBits = 0;
            for (const std::string& maskId : shape.maskLayerIds)
                ref.maskBits |= layers_.bitFor(maskId);
            if (ref.maskBits == 0) ref.maskBits = 1u;
            ref.instance = shapeInstance(transform, shape);
            ref.aabb = PhysicsMath::shapeWorldAabb(ref.instance);
            const size_t shape_index = shapes_.size();
            shapes_.push_back(std::move(ref));
            insertShapeIntoBroadphase(shape_index);
        }
        entityRanges_[id] = { start, shapes_.size() };
    }

    const std::vector<ShapeRef>& shapes() const {
        return shapes_;
    }

    bool overlapEntities(EntityId a, EntityId b, const Filter& filter = {}) const {
        for (const ShapeRef* sa : shapesFor(a)) {
            for (const ShapeRef* sb : shapesFor(b)) {
                if (!matchesFilter(*sb, filter)) continue;
                if (!canCollide(*sa, *sb)) continue;
                if (PhysicsMath::shapesOverlap(sa->instance, sb->instance))
                    return true;
            }
        }
        return false;
    }

    EntityId firstTouching(EntityId id, const Filter& filter = {}) const {
        for (const ShapeRef* self : shapesFor(id)) {
            for (const size_t other_index : queryAabb(self->aabb)) {
                const ShapeRef& other = shapes_[other_index];
                if (other.id == id) continue;
                if (!matchesFilter(other, filter)) continue;
                if (!canCollide(*self, other)) continue;
                if (PhysicsMath::shapesOverlap(self->instance, other.instance))
                    return other.id;
            }
        }
        return INVALID_ENTITY;
    }

    RaycastResult raycast(const Vec2& from,
                          const Vec2& to,
                          const Filter& filter = {}) const
    {
        RaycastResult result;
        PhysicsMath::RaycastHit best;
        const PhysicsMath::Aabb ray_bounds{
            std::min(from.x, to.x),
            std::min(from.y, to.y),
            std::max(from.x, to.x),
            std::max(from.y, to.y),
        };
        for (const size_t shape_index : queryAabb(ray_bounds)) {
            const ShapeRef& shape = shapes_[shape_index];
            if (shape.shape.response == CollisionResponse::Sensor) continue;
            if (!matchesFilter(shape, filter)) continue;
            const PhysicsMath::RaycastHit hit =
                PhysicsMath::raycastSegmentVsShape(from, to, shape.instance);
            if (!hit.hit || hit.fraction >= best.fraction)
                continue;
            best = hit;
            result.hit = true;
            result.entityId = shape.id;
            result.shapeIndex = shape.shapeIndex;
            result.point = hit.point;
        }
        if (result.hit) {
            const float dx = result.point.x - from.x;
            const float dy = result.point.y - from.y;
            result.distance = std::sqrt(dx * dx + dy * dy);
        }
        return result;
    }

    bool isGrounded(EntityId id) const {
        // ADR-0022: thin geometric probe (1 px). Prefer World::findGroundSupport
        // / collisionGrounded(velocity) for One Way policy.
        constexpr float kSkin = 1.0f;
        Filter ground;
        ground.response = "solid";
        for (const ShapeRef* feet : shapesFor(id)) {
            if (feet->shape.role != CollisionShapeRole::Feet
                && feet->shape.role != CollisionShapeRole::Body)
                continue;
            PhysicsMath::Aabb probe = feet->aabb;
            probe.minY = probe.maxY - kSkin;
            probe.maxY += kSkin;
            PhysicsMath::ShapeInstance probeShape;
            probeShape.shape = ColliderShape::Rectangle;
            probeShape.position = {
                (probe.minX + probe.maxX) * 0.5f,
                (probe.minY + probe.maxY) * 0.5f,
            };
            probeShape.size = {
                std::max(1.f, probe.maxX - probe.minX),
                std::max(1.f, probe.maxY - probe.minY),
            };
            for (const size_t other_index : queryAabb(probe)) {
                const ShapeRef& other = shapes_[other_index];
                if (other.id == id || other.shape.response != CollisionResponse::Solid)
                    continue;
                if (!canCollide(*feet, other)) continue;
                if (PhysicsMath::aabbOverlap(probe, other.aabb)
                    && PhysicsMath::shapesOverlap(probeShape, other.instance)
                    && feet->aabb.maxY <= other.aabb.minY + kSkin)
                    return true;
            }
        }
        return false;
    }

    std::vector<ContactEvent> refreshEvents() {
        std::unordered_map<ShapePairKey, ContactEvent, ShapePairKeyHash> current;
        current.reserve(shapes_.size() * 2);
        std::vector<ContactEvent> events;
        for (size_t i = 0; i < shapes_.size(); ++i) {
            for (const size_t j : queryAabb(shapes_[i].aabb)) {
                if (j <= i) continue;
                const ShapeRef& a = shapes_[i];
                const ShapeRef& b = shapes_[j];
                if (a.id == b.id || !canCollide(a, b)) continue;
                if (!PhysicsMath::shapesOverlap(a.instance, b.instance)) continue;
                const ShapePairKey key = pairKey(a, b);
                const auto previous = activePairs_.find(key);
                ContactEvent ev = makeEvent(
                    previous != activePairs_.end()
                        ? ContactEvent::Kind::Stay
                        : ContactEvent::Kind::Enter,
                    a, b);
                current.emplace(key, ev);
                events.push_back(ev);
            }
        }
        std::vector<std::pair<ShapePairKey, ContactEvent>> exits;
        for (const auto& [key, previous] : activePairs_) {
            if (current.count(key) == 0) {
                ContactEvent exit = previous;
                exit.kind = ContactEvent::Kind::Exit;
                exits.push_back({ key, exit });
            }
        }
        std::sort(exits.begin(), exits.end(),
            [](const auto& left, const auto& right) {
                if (left.first.a != right.first.a)
                    return left.first.a < right.first.a;
                return left.first.b < right.first.b;
            });
        for (const auto& item : exits)
            events.push_back(item.second);
        activePairs_ = std::move(current);
        return events;
    }

private:
    static constexpr float SPATIAL_CELL_SIZE = 128.f;

    std::vector<const ShapeRef*> shapesFor(EntityId id) const {
        std::vector<const ShapeRef*> out;
        auto it = entityRanges_.find(id);
        if (it == entityRanges_.end()) return out;
        out.reserve(it->second.second - it->second.first);
        for (size_t i = it->second.first; i < it->second.second; ++i)
            out.push_back(&shapes_[i]);
        return out;
    }

    static int cellCoord(float value) {
        return static_cast<int>(std::floor(value / SPATIAL_CELL_SIZE));
    }

    static void cellRange(const PhysicsMath::Aabb& aabb,
                          int& min_x,
                          int& min_y,
                          int& max_x,
                          int& max_y) {
        min_x = cellCoord(aabb.minX);
        min_y = cellCoord(aabb.minY);
        max_x = cellCoord(aabb.maxX);
        max_y = cellCoord(aabb.maxY);
    }

    std::vector<size_t> allShapeIndices() const {
        std::vector<size_t> out;
        out.reserve(shapes_.size());
        for (size_t i = 0; i < shapes_.size(); ++i)
            out.push_back(i);
        return out;
    }

    void insertShapeIntoBroadphase(size_t shape_index) {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        cellRange(shapes_[shape_index].aabb, min_x, min_y, max_x, max_y);
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x)
                spatialCells_[SpatialCell{ x, y }].push_back(shape_index);
        }
    }

    std::vector<size_t> queryAabb(const PhysicsMath::Aabb& aabb) const {
        if (shapes_.empty()) return {};

        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        cellRange(aabb, min_x, min_y, max_x, max_y);

        const int64_t cell_count_x =
            static_cast<int64_t>(max_x) - static_cast<int64_t>(min_x) + 1;
        const int64_t cell_count_y =
            static_cast<int64_t>(max_y) - static_cast<int64_t>(min_y) + 1;
        const uint64_t cell_count =
            static_cast<uint64_t>(cell_count_x)
            * static_cast<uint64_t>(cell_count_y);
        if (cell_count > static_cast<uint64_t>(shapes_.size() * 2 + 64))
            return allShapeIndices();

        std::vector<size_t> out;
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const auto cell = spatialCells_.find(SpatialCell{ x, y });
                if (cell == spatialCells_.end()) continue;
                for (const size_t shape_index : cell->second)
                    out.push_back(shape_index);
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    static uint64_t shapeKey(const ShapeRef& shape) {
        return (static_cast<uint64_t>(shape.id) << 32)
            | static_cast<uint64_t>(shape.shapeIndex & 0xffffffffu);
    }

    static ShapePairKey pairKey(const ShapeRef& a, const ShapeRef& b) {
        uint64_t left = shapeKey(a);
        uint64_t right = shapeKey(b);
        if (left > right) std::swap(left, right);
        return ShapePairKey{ left, right };
    }

    static ContactEvent makeEvent(ContactEvent::Kind kind,
                                  const ShapeRef& a,
                                  const ShapeRef& b) {
        ContactEvent ev;
        ev.kind = kind;
        ev.self = a.id;
        ev.other = b.id;
        ev.selfRole = roleName(a.shape.role);
        ev.otherRole = roleName(b.shape.role);
        ev.selfResponse = responseName(a.shape.response);
        ev.otherResponse = responseName(b.shape.response);
        ev.selfLayerId = a.shape.layerId;
        ev.otherLayerId = b.shape.layerId;
        ev.point = {
            (std::max(a.aabb.minX, b.aabb.minX) + std::min(a.aabb.maxX, b.aabb.maxX)) * 0.5f,
            (std::max(a.aabb.minY, b.aabb.minY) + std::min(a.aabb.maxY, b.aabb.maxY)) * 0.5f,
        };
        const float ax = (a.aabb.minX + a.aabb.maxX) * 0.5f;
        const float ay = (a.aabb.minY + a.aabb.maxY) * 0.5f;
        const float bx = (b.aabb.minX + b.aabb.maxX) * 0.5f;
        const float by = (b.aabb.minY + b.aabb.maxY) * 0.5f;
        ev.normal = { bx >= ax ? 1.f : -1.f, by >= ay ? 1.f : -1.f };
        return ev;
    }

    LayerTable layers_;
    std::vector<ShapeRef> shapes_;
    std::unordered_map<EntityId, std::pair<size_t, size_t>> entityRanges_;
    std::unordered_map<SpatialCell, std::vector<size_t>, SpatialCellHash> spatialCells_;
    std::unordered_map<ShapePairKey, ContactEvent, ShapePairKeyHash> activePairs_;
};

} // namespace ArtCade::CollisionWorld
