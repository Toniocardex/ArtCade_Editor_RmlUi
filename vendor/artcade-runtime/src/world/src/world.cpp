#include "../include/world.h"
#include "../../modules/scene-system/include/scene-lifecycle-service.h"
#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../modules/sprite-animator/include/sprite-animator.h"
#include "../../modules/physics/include/physics.h"
#include "../../modules/variable-manager/include/variable-manager.h"
#include "../../modules/renderer/include/renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace ArtCade {

namespace {

constexpr EntityId kTileCollisionEntityStart = 0x80000000u;

bool nearly_equal(float a, float b, float epsilon = 0.01f) {
    return std::fabs(a - b) <= epsilon;
}

struct TileAggregateKey {
    CollisionResponse response = CollisionResponse::Solid;
    CollisionShapeRole role = CollisionShapeRole::Body;
    std::string layerId;
    std::vector<std::string> maskLayerIds;
    bool oneWay = false;
    float friction = 0.f;
    float restitution = 0.f;
    float density = 0.f;

    bool operator==(const TileAggregateKey& other) const {
        return response == other.response
            && role == other.role
            && layerId == other.layerId
            && maskLayerIds == other.maskLayerIds
            && oneWay == other.oneWay
            && friction == other.friction
            && restitution == other.restitution
            && density == other.density;
    }
};

TileAggregateKey aggregate_key_for_shape(const CollisionShape& shape) {
    TileAggregateKey key;
    key.response = shape.response;
    key.role = shape.role;
    key.layerId = shape.layerId;
    key.maskLayerIds = shape.maskLayerIds;
    std::sort(key.maskLayerIds.begin(), key.maskLayerIds.end());
    key.oneWay = shape.oneWay;
    key.friction = shape.friction;
    key.restitution = shape.restitution;
    key.density = shape.density;
    return key;
}

bool is_full_cell_rect(const CollisionShape& shape, float tileSize) {
    if (!shape.enabled || shape.type != CollisionShapeType::Rectangle)
        return false;
    if (!nearly_equal(shape.offset.x, 0.f) || !nearly_equal(shape.offset.y, 0.f))
        return false;
    return (nearly_equal(shape.size.x, tileSize) && nearly_equal(shape.size.y, tileSize))
        || (nearly_equal(shape.size.x, 32.f) && nearly_equal(shape.size.y, 32.f));
}

struct TileCollisionCell {
    bool valid = false;
    TileAggregateKey key;
    CollisionShape shape;
};

TileCollisionCell aggregate_cell_for_tile(
    int tileId,
    const std::unordered_map<int, TileSurfaceMeta>& tileMeta,
    float tileSize)
{
    TileCollisionCell cell;
    auto it = tileMeta.find(tileId);
    if (it == tileMeta.end() || !it->second.collisionBody)
        return cell;
    const CollisionBodyComponent& body = *it->second.collisionBody;
    if (!body.enabled)
        return cell;

    const CollisionShape* fullCellShape = nullptr;
    for (const CollisionShape& shape : body.shapes) {
        if (!shape.enabled)
            continue;
        if (!is_full_cell_rect(shape, tileSize))
            return cell;
        if (fullCellShape != nullptr)
            return cell;
        fullCellShape = &shape;
    }
    if (!fullCellShape)
        return cell;

    cell.valid = true;
    cell.key = aggregate_key_for_shape(*fullCellShape);
    cell.shape = *fullCellShape;
    cell.shape.type = CollisionShapeType::Rectangle;
    cell.shape.offset = {};
    cell.shape.size = { tileSize, tileSize };
    cell.shape.radius = 0.f;
    cell.shape.points.clear();
    return cell;
}

} // namespace

World::World(Modules::RuntimeEntityGateway& gateway,
             Modules::Physics&              ph,
             Modules::VariableManager&      variables)
    : entityGateway_(gateway), physics_(ph), variables_(variables) {
    // Drop per-entity gameplay caches the moment the gateway destroys an
    // entity. EnTT recycles ids, so without this a fresh entity can inherit
    // previous gameplay timers and input state.
    entityGateway_.setEntityDestroyHandler([this](EntityId id) {
        const bool explicitGameplayDestroy = pendingGameplayDestroyIds_.erase(id) != 0;
        if (explicitGameplayDestroy) {
            destroyingEntityIds_.insert(id);
            if (entityWillDestroyHandler_) entityWillDestroyHandler_(id);
        }
        forgetEntity(id);
        variables_.destroyEntity(id);
        if (entityDestroyedHandler_) entityDestroyedHandler_(id);
        destroyingEntityIds_.erase(id);
    });
    entityGateway_.setEntityCreatedHandler([this](EntityId id, const EntityDef& def) {
        variables_.createEntity(id, def.localVariables, def.localVariableOverrides);
    });
}

void World::forgetEntity(EntityId id) {
    platformerRt_.erase(id);
    topDownRt_.erase(id);
    controlIntents_.erase(id);
    platformerStepContacts_.erase(id);
    if (spriteAnimator_) spriteAnimator_->removeEntity(id);
    if (cameraFollowMode_ == CameraFollowMode::Explicit
        && cameraFollowTarget_ == id) {
        useAutomaticCameraTarget();
    }
}

void World::setSpriteAnimator(Modules::SpriteAnimator* animator) {
    spriteAnimator_ = animator;
}

void World::setEntityWillDestroyHandler(std::function<void(EntityId)> handler) {
    entityWillDestroyHandler_ = std::move(handler);
}

void World::setEntityDestroyedHandler(std::function<void(EntityId)> handler) {
    entityDestroyedHandler_ = std::move(handler);
}

void World::clearGameplayRuntimeState() {
    platformerRt_.clear();
    topDownRt_.clear();
    controlIntents_.clear();
    platformerStepContacts_.clear();
    pendingGameplayDestroyIds_.clear();
    destroyingEntityIds_.clear();
    collisionEvents_.clear();
    useAutomaticCameraTarget();
}

void World::applyTilePalette(const std::vector<TilePaletteEntry>& tilePalette) {
    tileMeta_.clear();
    for (const auto& e : tilePalette) {
        if (e.id < 1) continue;
        TileSurfaceMeta m;
        m.collisionBody = e.collisionBody;
        if (m.collisionBody) {
            for (const CollisionShape& shape : m.collisionBody->shapes) {
                if (!shape.enabled) continue;
                if (shape.response == CollisionResponse::Solid) {
                    m.blocks = true;
                    m.oneWay = m.oneWay || shape.oneWay;
                }
            }
        }
        tileMeta_[e.id] = std::move(m);
    }
}

void World::syncAfterEditorProject(const std::vector<TilePaletteEntry>& tilePalette) {
    applyTilePalette(tilePalette);
    clearGameplayRuntimeState();
    rebuildCollisionWorld();
}

void World::restoreDesignState(const std::vector<TilePaletteEntry>& tilePalette) {
    syncAfterEditorProject(tilePalette);
}

void World::init(const ProjectDoc& doc) {
    clearGameplayRuntimeState();
    physicsLayers_ = doc.physicsLayers;
    collisionWorld_.setLayers(physicsLayers_);
    variables_.configureGlobals(doc.globalVariables);
    entityGateway_.setPhysics(&physics_);
    const std::unordered_map<std::string, EntityDef>* typesPtr =
        doc.objectTypes.empty() ? nullptr : &doc.objectTypes;
    entityGateway_.replaceProject(doc.scenes, doc.entities, doc.activeSceneId, typesPtr);
    entityGateway_.setCollisionProjectData(
        doc.physicsLayers, doc.collisionProfiles, doc.spritePathToAssetId);

    applyTilePalette(doc.tilePalette);
    activeTilemap_ = TilemapData{};

    // ADR-0018: snap to Camera Target or cameraStart+viewport/2 (clamped).
    // Never start at world centre then ease toward the player.
    resetCameraForActiveScene();

    rebuildCollisionWorld();
}

void World::shutdown() {
    // Unregister the destroy hook before anything else: the lambda captures
    // `this`, and if World is destroyed before the gateway any later
    // destroy(id) on the gateway would dereference dangling memory.
    entityGateway_.setEntityDestroyHandler(nullptr);
    entityGateway_.setEntityCreatedHandler(nullptr);
    entityGateway_.setPhysicsTopologyHandler(nullptr);
    entityWillDestroyHandler_ = {};
    entityDestroyedHandler_ = {};

    variables_.clear();
    clearGameplayRuntimeState();
    activeTilemap_ = TilemapData{};
    tileMeta_.clear();
    collisionEvents_.clear();
    collisionWorld_.clear();
    physicsLayers_.clear();
}

bool World::isActiveEntity(EntityId id) const {
    return entityGateway_.isEntityActiveInScene(id);
}

bool World::isObjectType(EntityId id, const ObjectTypeId& expected) const {
    return !expected.empty() && isActiveEntity(id)
        && entityGateway_.className(id) == expected;
}

bool World::requestDestroy(EntityId id) {
    if (!isActiveEntity(id)) return false;
    if (pendingGameplayDestroyIds_.count(id) || destroyingEntityIds_.count(id)) return true;
    pendingGameplayDestroyIds_.insert(id);
    entityGateway_.queueDestroy(id);
    return true;
}

bool World::playAnimationClip(EntityId id, const AssetId& animationAssetId,
                              const std::string& clipId) {
    if (!spriteAnimator_ || !isActiveEntity(id) || animationAssetId.empty()
        || clipId.empty()) return false;
    SpriteRendererComponent renderer;
    SpriteAnimatorComponent animator;
    if (!entityGateway_.getSpriteRenderer(id, renderer)
        || !entityGateway_.getSpriteAnimator(id, animator)
        || !spriteAnimator_->isClipPlayable(animationAssetId, clipId)) {
        return false;
    }
    return spriteAnimator_->play(id, animationAssetId, clipId);
}

bool World::stopAnimation(EntityId id) {
    if (!spriteAnimator_ || !isActiveEntity(id)) return false;
    SpriteRendererComponent renderer;
    SpriteAnimatorComponent animator;
    if (!entityGateway_.getSpriteRenderer(id, renderer)
        || !entityGateway_.getSpriteAnimator(id, animator)) return false;
    spriteAnimator_->stop(id);
    return true;
}

bool World::setAnimationPlaybackSpeed(EntityId id, float speed) {
    if (!spriteAnimator_ || !isActiveEntity(id) || !std::isfinite(speed) || speed <= 0.f)
        return false;
    SpriteRendererComponent renderer;
    SpriteAnimatorComponent animator;
    if (!entityGateway_.getSpriteRenderer(id, renderer)
        || !entityGateway_.getSpriteAnimator(id, animator)) return false;
    return spriteAnimator_->setPlaybackSpeed(id, speed);
}

void World::onSceneActivated() {
    clearGameplayRuntimeState();
    resetCameraForActiveScene();
}

void World::setSceneLifecycleService(Modules::SceneLifecycleService* lifecycle) {
    lifecycle_ = lifecycle;
}

bool World::loadScene(const SceneId& id) {
    if (lifecycle_) {
        const auto result = lifecycle_->load_immediate(id);
        if (!result.changed) return false;
        rebuildCollisionWorld();
        return true;
    }
    if (!entityGateway_.loadScene(id)) return false;
    clearGameplayRuntimeState();
    rebuildCollisionWorld();
    return true;
}

SceneId World::activeSceneId() const {
    return entityGateway_.activeSceneId();
}

void World::syncPhysicsToEntities() {
    // EnTT visitor: in-place Transform update for every active entity that
    // has a live physics handle. Platformer and Top Down own Transform and
    // push any Physics handle from their kinematic steps (ADR-0021).
    entityGateway_.forEachActivePhysicsBody(
        [this](EntityId id, uint32_t handle, Transform& t) {
            PlatformerControllerComponent platformer{};
            if (entityGateway_.getPlatformerController(id, platformer))
                return;
            TopDownControllerComponent topDown{};
            if (entityGateway_.getTopDownController(id, topDown))
                return;
            t.position = physics_.getPosition(handle);
            t.velocity = physics_.getLinearVelocity(handle);
        });
    rebuildCollisionWorld();
}

bool World::hasGlobalState(const std::string& key) const {
    return variables_.exists(key);
}

StateValue World::getGlobalState(const std::string& key) const {
    auto v = variables_.get(key);
    if (auto* number = std::get_if<double>(&v)) return StateValue{ static_cast<float>(*number) };
    if (auto* b = std::get_if<bool>(&v))      return StateValue{ *b };
    if (auto* s = std::get_if<std::string>(&v)) return StateValue{ *s };
    return StateValue{ 0 };
}

void World::setGlobalState(const std::string& key, const StateValue& value) {
    if (auto* i = std::get_if<int32_t>(&value)) {
        (void)variables_.setGlobal(key, static_cast<double>(*i));
    } else if (auto* f = std::get_if<float>(&value)) {
        (void)variables_.setGlobal(key, static_cast<double>(*f));
    } else if (auto* b = std::get_if<bool>(&value)) {
        (void)variables_.setGlobal(key, *b);
    } else if (auto* s = std::get_if<std::string>(&value)) {
        (void)variables_.setGlobal(key, *s);
    }
}

std::vector<EntityId> World::activeEntityIds() const {
    return entityGateway_.activeSceneIds();
}

void World::rebuildCollisionWorld() {
    if (const SceneDef* scene = entityGateway_.activeScene())
        activeTilemap_ = scene->tilemap;
    collisionWorld_.clear();
    const auto& layers = entityGateway_.physicsLayers();
    if (layers.empty())
        collisionWorld_.setLayers({});
    else
        collisionWorld_.setLayers(layers);
    entityGateway_.forEachActiveCollisionBody(
        [this](EntityId id,
               const Transform& transform,
               const CollisionBodyComponent& body) {
            collisionWorld_.addEntity(id, transform, body);
        });

    const TilemapData& tm = activeTilemap_;
    if (tm.cols <= 0 || tm.rows <= 0 || tm.tileSize <= 0.f)
        return;

    const float ts = tm.tileSize;

    auto tileAt = [&](int col, int row) -> int {
        if (col < 0 || row < 0 || col >= tm.cols || row >= tm.rows)
            return 0;
        const int idx = row * tm.cols + col;
        if (idx < 0 || idx >= static_cast<int>(tm.data.size()))
            return 0;
        return tm.data[idx];
    };

    std::vector<TileCollisionCell> cells(static_cast<size_t>(tm.cols * tm.rows));
    for (int row = 0; row < tm.rows; ++row) {
        for (int col = 0; col < tm.cols; ++col) {
            const int idx = row * tm.cols + col;
            cells[static_cast<size_t>(idx)] =
                aggregate_cell_for_tile(tileAt(col, row), tileMeta_, ts);
        }
    }

    auto cellAt = [&](int col, int row) -> const TileCollisionCell& {
        return cells[static_cast<size_t>(row * tm.cols + col)];
    };

    EntityId tileEntityId = kTileCollisionEntityStart;
    std::vector<uint8_t> consumed(cells.size(), 0);

    for (int row = 0; row < tm.rows; ++row) {
        for (int col = 0; col < tm.cols; ++col) {
            const int idx = row * tm.cols + col;
            if (consumed[static_cast<size_t>(idx)])
                continue;
            const TileCollisionCell& seed = cellAt(col, row);
            if (!seed.valid)
                continue;

            int width = 1;
            while (col + width < tm.cols) {
                const int rightIdx = row * tm.cols + col + width;
                const TileCollisionCell& right = cellAt(col + width, row);
                if (consumed[static_cast<size_t>(rightIdx)]
                    || !right.valid
                    || !(right.key == seed.key))
                    break;
                ++width;
            }

            int height = 1;
            bool canGrow = true;
            while (row + height < tm.rows && canGrow) {
                for (int x = 0; x < width; ++x) {
                    const int growIdx = (row + height) * tm.cols + col + x;
                    const TileCollisionCell& grow = cellAt(col + x, row + height);
                    if (consumed[static_cast<size_t>(growIdx)]
                        || !grow.valid
                        || !(grow.key == seed.key)) {
                        canGrow = false;
                        break;
                    }
                }
                if (canGrow)
                    ++height;
            }

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    consumed[static_cast<size_t>((row + y) * tm.cols + col + x)] = 1;
                }
            }

            CollisionBodyComponent body;
            body.bodyType = BodyType::Static;
            body.enabled = true;
            CollisionShape aggregate = seed.shape;
            aggregate.size = { width * ts, height * ts };
            body.shapes.push_back(std::move(aggregate));

            Transform tileTransform{};
            tileTransform.position = {
                col * ts + width * ts * 0.5f,
                row * ts + height * ts * 0.5f,
            };
            collisionWorld_.addEntity(tileEntityId++, tileTransform, body);
        }
    }

    for (int row = 0; row < tm.rows; ++row) {
        for (int col = 0; col < tm.cols; ++col) {
            const TileCollisionCell& aggregateCell = cellAt(col, row);
            if (aggregateCell.valid)
                continue;
            const int tileId = tileAt(col, row);
            auto meta = tileMeta_.find(tileId);
            if (meta == tileMeta_.end() || !meta->second.collisionBody)
                continue;
            if (!meta->second.collisionBody->enabled)
                continue;

            CollisionBodyComponent body = *meta->second.collisionBody;
            body.bodyType = BodyType::Static;
            body.enabled = true;

            Transform tileTransform{};
            tileTransform.position = {
                col * ts + ts * 0.5f,
                row * ts + ts * 0.5f,
            };
            collisionWorld_.addEntity(tileEntityId++, tileTransform, body);
        }
    }
}

bool World::collisionOverlap(EntityId a, EntityId b) const {
    return collisionWorld_.overlapEntities(a, b);
}

EntityId World::firstCollisionTouching(
    EntityId id,
    const CollisionWorld::Filter& filter) const
{
    if (!filter.className.empty()) {
        for (EntityId other : entityGateway_.poolByClass(filter.className)) {
            if (other != id && collisionWorld_.overlapEntities(id, other, filter))
                return other;
        }
        return INVALID_ENTITY;
    }
    if (!filter.tag.empty()) {
        for (EntityId other : entityGateway_.byTag(filter.tag)) {
            if (other != id && collisionWorld_.overlapEntities(id, other, filter))
                return other;
        }
        return INVALID_ENTITY;
    }
    return collisionWorld_.firstTouching(id, filter);
}

size_t World::collisionShapeCount() const {
    return collisionWorld_.shapes().size();
}

const std::vector<CollisionWorld::ShapeRef>& World::collisionShapes() const {
    return collisionWorld_.shapes();
}

const std::vector<CollisionWorld::ContactEvent>& World::collisionEvents() const {
    return collisionEvents_;
}

namespace {

bool contact_kind_matches(CollisionWorld::ContactEvent::Kind eventKind,
                          const std::string& kind) {
    if (kind.empty()) return true;
    if (kind == "enter") return eventKind == CollisionWorld::ContactEvent::Kind::Enter;
    if (kind == "stay") return eventKind == CollisionWorld::ContactEvent::Kind::Stay;
    if (kind == "exit") return eventKind == CollisionWorld::ContactEvent::Kind::Exit;
    return false;
}

CollisionWorld::ContactEvent event_for_entity(
    const CollisionWorld::ContactEvent& event,
    EntityId id) {
    if (event.self == id)
        return event;
    CollisionWorld::ContactEvent out = event;
    out.self = event.other;
    out.other = event.self;
    std::swap(out.selfRole, out.otherRole);
    std::swap(out.selfResponse, out.otherResponse);
    std::swap(out.selfLayerId, out.otherLayerId);
    out.normal.x = -out.normal.x;
    out.normal.y = -out.normal.y;
    return out;
}

} // namespace

std::vector<CollisionWorld::ContactEvent> World::collisionEventsFor(
    EntityId id,
    const std::string& kind,
    const CollisionWorld::Filter& filter) const
{
    std::vector<CollisionWorld::ContactEvent> out;
    for (const CollisionWorld::ContactEvent& raw : collisionEvents_) {
        if (raw.self != id && raw.other != id)
            continue;
        if (!contact_kind_matches(raw.kind, kind))
            continue;
        CollisionWorld::ContactEvent event = event_for_entity(raw, id);
        if (!filter.layerId.empty() && event.otherLayerId != filter.layerId)
            continue;
        if (!filter.role.empty() && event.otherRole != filter.role)
            continue;
        if (!filter.response.empty() && event.otherResponse != filter.response)
            continue;
        if (!filter.className.empty()) {
            bool classMatch = false;
            for (EntityId other : entityGateway_.poolByClass(filter.className)) {
                if (other == event.other) {
                    classMatch = true;
                    break;
                }
            }
            if (!classMatch)
                continue;
        }
        if (!filter.tag.empty()) {
            bool tagMatch = false;
            for (EntityId other : entityGateway_.byTag(filter.tag)) {
                if (other == event.other) {
                    tagMatch = true;
                    break;
                }
            }
            if (!tagMatch)
                continue;
        }
        out.push_back(std::move(event));
    }
    return out;
}

bool World::hasCollisionEvent(
    EntityId id,
    const std::string& kind,
    const CollisionWorld::Filter& filter) const
{
    return !collisionEventsFor(id, kind, filter).empty();
}

CollisionWorld::RaycastResult World::collisionRaycast(
    const Vec2& from,
    const Vec2& to,
    const CollisionWorld::Filter& filter) const
{
    return collisionWorld_.raycast(from, to, filter);
}

GroundSupportPolicy groundSupportPolicyFor(float supportWidth, float supportHeight) {
    GroundSupportPolicy policy;
    policy.contactSkin = kGroundContactSkin;
    if (!std::isfinite(supportWidth) || !std::isfinite(supportHeight)
        || supportWidth <= 0.f || supportHeight <= 0.f) {
        policy.minHorizontalOverlap = std::numeric_limits<float>::infinity();
        policy.bodyInsetX = 0.f;
        policy.maxFloorSnapDistance = 0.f;
        return policy;
    }
    policy.bodyInsetX =
        std::clamp(supportWidth * 0.05f, 0.25f, 2.0f);
    policy.minHorizontalOverlap =
        std::clamp(supportWidth * 0.05f, 0.5f, 2.0f);
    policy.maxFloorSnapDistance =
        std::clamp(supportHeight * 0.10f, 0.5f, 4.0f);
    return policy;
}

bool World::collisionGrounded(EntityId id) const {
    return collisionGrounded(id, /*verticalVelocity=*/0.f);
}

bool World::collisionGrounded(EntityId id, float verticalVelocity) const {
    Transform current{};
    if (!entityGateway_.getTransform(id, current))
        return false;
    return findGroundSupport(
        id, current, current, verticalVelocity, /*allowFloorSnap=*/false)
        .has_value();
}

std::optional<GroundSupport> World::findGroundSupport(
    EntityId id,
    const Transform& current,
    const Transform& previous,
    float requestedVerticalVelocity,
    bool allowFloorSnap) const
{
    if (!(requestedVerticalVelocity >= 0.f))
        return std::nullopt;

    const auto& shapes = collisionWorld_.shapes();
    bool hasFeet = false;
    for (const CollisionWorld::ShapeRef& authoredSelf : shapes) {
        if (authoredSelf.id != id)
            continue;
        if (!authoredSelf.shape.enabled
            || authoredSelf.shape.response != CollisionResponse::Solid)
            continue;
        if (authoredSelf.shape.role == CollisionShapeRole::Feet) {
            hasFeet = true;
            break;
        }
    }

    std::optional<GroundSupport> best;
    for (const CollisionWorld::ShapeRef& authoredSelf : shapes) {
        if (authoredSelf.id != id)
            continue;
        if (!authoredSelf.shape.enabled
            || authoredSelf.shape.response != CollisionResponse::Solid)
            continue;
        if (hasFeet) {
            if (authoredSelf.shape.role != CollisionShapeRole::Feet)
                continue;
        } else if (authoredSelf.shape.role != CollisionShapeRole::Body) {
            continue;
        }

        const PhysicsMath::ShapeInstance selfInst =
            CollisionWorld::shapeInstance(current, authoredSelf.shape);
        const PhysicsMath::Aabb selfAabb = PhysicsMath::shapeWorldAabb(selfInst);
        const PhysicsMath::Aabb previousSelfAabb = PhysicsMath::shapeWorldAabb(
            CollisionWorld::shapeInstance(previous, authoredSelf.shape));

        if (!std::isfinite(selfAabb.minX) || !std::isfinite(selfAabb.maxX)
            || !std::isfinite(selfAabb.minY) || !std::isfinite(selfAabb.maxY))
            continue;

        const float supportWidth = selfAabb.maxX - selfAabb.minX;
        const float supportHeight = selfAabb.maxY - selfAabb.minY;
        const GroundSupportPolicy policy =
            groundSupportPolicyFor(supportWidth, supportHeight);
        if (!std::isfinite(policy.minHorizontalOverlap))
            continue;

        PhysicsMath::Aabb probeAabb = selfAabb;
        if (!hasFeet) {
            if (supportWidth <= 2.f * policy.bodyInsetX)
                continue;
            probeAabb.minX += policy.bodyInsetX;
            probeAabb.maxX -= policy.bodyInsetX;
        }

        CollisionWorld::ShapeRef selfRef = authoredSelf;
        selfRef.instance = selfInst;
        selfRef.aabb = selfAabb;

        for (const CollisionWorld::ShapeRef& other : shapes) {
            if (other.id == id)
                continue;
            if (!other.shape.enabled
                || other.shape.response != CollisionResponse::Solid)
                continue;
            if (!std::isfinite(other.aabb.minX) || !std::isfinite(other.aabb.maxX)
                || !std::isfinite(other.aabb.minY) || !std::isfinite(other.aabb.maxY))
                continue;
            if ((other.aabb.maxX - other.aabb.minX) <= 0.f
                || (other.aabb.maxY - other.aabb.minY) <= 0.f)
                continue;
            if (!CollisionWorld::canCollide(selfRef, other))
                continue;

            const float horizontalOverlap =
                std::min(probeAabb.maxX, other.aabb.maxX)
                - std::max(probeAabb.minX, other.aabb.minX);
            if (horizontalOverlap < policy.minHorizontalOverlap)
                continue;

            const float gap = other.aabb.minY - probeAabb.maxY;
            if (allowFloorSnap) {
                if (gap < -policy.contactSkin
                    || gap > policy.maxFloorSnapDistance)
                    continue;
            } else if (std::fabs(gap) > policy.contactSkin) {
                continue;
            }

            if (other.shape.oneWay) {
                if (previousSelfAabb.maxY
                    > other.aabb.minY + kOneWayApproachTolerance)
                    continue;
            }

            GroundSupport candidate;
            candidate.supportEntityId = other.id;
            candidate.selfShapeIndex = authoredSelf.shapeIndex;
            candidate.supportShapeIndex = other.shapeIndex;
            candidate.supportTopY = other.aabb.minY;
            candidate.correctionY = gap;
            candidate.oneWay = other.shape.oneWay;
            candidate.horizontalOverlap = horizontalOverlap;
            candidate.feetBottomY = probeAabb.maxY;

            const auto rank = [](const GroundSupport& s) {
                return std::tuple{
                    std::fabs(s.correctionY),
                    s.oneWay ? 1 : 0,
                    -s.horizontalOverlap,
                    s.supportTopY,
                    s.supportEntityId,
                    s.supportShapeIndex,
                    s.selfShapeIndex,
                };
            };
            if (!best.has_value() || rank(candidate) < rank(*best))
                best = candidate;
        }
    }
    return best;
}

KinematicCollisionResult World::resolveKinematicCollisionBody(
    EntityId id,
    Transform& transform,
    const Transform& beforeMove,
    float& horizontalVelocity,
    float& verticalVelocity) const
{
    KinematicCollisionResult result{};
    CollisionBodyComponent selfBody{};
    if (!entityGateway_.getResolvedCollisionBody(id, selfBody) || !selfBody.enabled)
        return result;

    const float requestedVerticalVelocity = verticalVelocity;
    const bool descendingOrResting = requestedVerticalVelocity >= 0.f;

    const Vec2 delta{
        transform.position.x - beforeMove.position.x,
        transform.position.y - beforeMove.position.y,
    };
    if (std::abs(delta.x) > 1e-6f || std::abs(delta.y) > 1e-6f) {
        bool hitAny = false;
        PhysicsMath::SweepHit bestHit;
        for (const CollisionWorld::ShapeRef& authoredSelf : collisionWorld_.shapes()) {
            if (authoredSelf.id != id)
                continue;
            if (!authoredSelf.shape.enabled
                || authoredSelf.shape.response != CollisionResponse::Solid)
                continue;
            if (authoredSelf.shape.role != CollisionShapeRole::Body
                && authoredSelf.shape.role != CollisionShapeRole::Feet)
                continue;

            CollisionWorld::ShapeRef moving = authoredSelf;
            moving.instance =
                CollisionWorld::shapeInstance(beforeMove, moving.shape);
            moving.aabb = PhysicsMath::shapeWorldAabb(moving.instance);

            for (const CollisionWorld::ShapeRef& other : collisionWorld_.shapes()) {
                if (other.id == id)
                    continue;
                if (!other.shape.enabled
                    || other.shape.response != CollisionResponse::Solid)
                    continue;
                if (!CollisionWorld::canCollide(moving, other))
                    continue;
                if (other.shape.oneWay
                    && !(requestedVerticalVelocity >= 0.f
                         && moving.aabb.maxY
                                <= other.aabb.minY + kOneWayApproachTolerance))
                    continue;

                const PhysicsMath::SweepHit hit =
                    PhysicsMath::sweepAabb(moving.aabb, delta, other.aabb);
                if (!hit.hit || hit.fraction >= bestHit.fraction)
                    continue;
                bestHit = hit;
                hitAny = true;
            }
        }

        if (hitAny) {
            const float t = std::max(0.f, bestHit.fraction - 1e-4f);
            transform.position.x = beforeMove.position.x + delta.x * t;
            transform.position.y = beforeMove.position.y + delta.y * t;
            if (std::abs(bestHit.normal.x) > 0.f)
                horizontalVelocity = 0.f;
            if (std::abs(bestHit.normal.y) > 0.f)
                verticalVelocity = 0.f;
        }
    }

    for (int pass = 0; pass < 4; ++pass) {
        bool resolvedAny = false;
        for (const CollisionWorld::ShapeRef& authoredSelf : collisionWorld_.shapes()) {
            if (authoredSelf.id != id)
                continue;
            if (!authoredSelf.shape.enabled
                || authoredSelf.shape.response != CollisionResponse::Solid)
                continue;
            if (authoredSelf.shape.role != CollisionShapeRole::Body
                && authoredSelf.shape.role != CollisionShapeRole::Feet)
                continue;

            CollisionWorld::ShapeRef selfRef = authoredSelf;
            selfRef.instance =
                CollisionWorld::shapeInstance(transform, selfRef.shape);
            selfRef.aabb = PhysicsMath::shapeWorldAabb(selfRef.instance);

            const auto prevAabb = PhysicsMath::shapeWorldAabb(
                CollisionWorld::shapeInstance(beforeMove, selfRef.shape));

            for (const CollisionWorld::ShapeRef& other : collisionWorld_.shapes()) {
                if (resolvedAny || other.id == id)
                    break;
                if (!other.shape.enabled
                    || other.shape.response != CollisionResponse::Solid)
                    continue;
                if (!CollisionWorld::canCollide(selfRef, other))
                    continue;
                if (!PhysicsMath::aabbOverlap(selfRef.aabb, other.aabb))
                    continue;
                if (!PhysicsMath::shapesOverlap(selfRef.instance, other.instance))
                    continue;
                if (other.shape.oneWay
                    && !(requestedVerticalVelocity >= 0.f
                         && prevAabb.maxY
                                <= other.aabb.minY + kOneWayApproachTolerance))
                    continue;

                Vec2 correction{};
                if (!PhysicsMath::resolveAabbSeparation(selfRef.aabb, other.aabb, correction))
                    continue;
                transform.position.x += correction.x;
                transform.position.y += correction.y;
                if (std::abs(correction.x) > 1e-6f) horizontalVelocity = 0.f;
                if (std::abs(correction.y) > 1e-6f) verticalVelocity = 0.f;
                resolvedAny = true;
            }
        }
        if (!resolvedAny) break;
    }

    if (descendingOrResting) {
        if (const auto support = findGroundSupport(
                id, transform, beforeMove, requestedVerticalVelocity,
                /*allowFloorSnap=*/false)) {
            transform.position.y += support->correctionY;
            verticalVelocity = 0.f;
            result.grounded = true;
            result.groundEntityId = support->supportEntityId;
        }
    }
    return result;
}

namespace {

bool isPlatformerSolidRole(CollisionShapeRole role) {
    return role == CollisionShapeRole::Body || role == CollisionShapeRole::Feet;
}

bool oneWayEligibleFromAbove(
    float requestedVerticalVelocity,
    float previousFeetBottomY,
    float supportTopY)
{
    return requestedVerticalVelocity >= 0.f
        && previousFeetBottomY <= supportTopY + kOneWayApproachTolerance;
}

bool resolveAabbSeparationAxisX(
    const PhysicsMath::Aabb& movable,
    const PhysicsMath::Aabb& fixed,
    float& outCorrectionX)
{
    if (!PhysicsMath::aabbOverlap(movable, fixed))
        return false;
    const float penLeft = movable.maxX - fixed.minX;
    const float penRight = fixed.maxX - movable.minX;
    const float penX = std::min(penLeft, penRight);
    if (!(penX > 0.f) || !std::isfinite(penX))
        return false;
    const float movableCx = (movable.minX + movable.maxX) * 0.5f;
    const float fixedCx = (fixed.minX + fixed.maxX) * 0.5f;
    outCorrectionX = (movableCx < fixedCx) ? -penX : penX;
    return true;
}

bool resolveAabbSeparationAxisY(
    const PhysicsMath::Aabb& movable,
    const PhysicsMath::Aabb& fixed,
    float& outCorrectionY)
{
    if (!PhysicsMath::aabbOverlap(movable, fixed))
        return false;
    const float penUp = movable.maxY - fixed.minY;
    const float penDown = fixed.maxY - movable.minY;
    const float penY = std::min(penUp, penDown);
    if (!(penY > 0.f) || !std::isfinite(penY))
        return false;
    const float movableCy = (movable.minY + movable.maxY) * 0.5f;
    const float fixedCy = (fixed.minY + fixed.maxY) * 0.5f;
    outCorrectionY = (movableCy < fixedCy) ? -penY : penY;
    return true;
}

} // namespace

PlatformerAxisMoveResult World::movePlatformerX(
    EntityId id,
    Transform& transform,
    const Transform& beforeMove,
    float& horizontalVelocity) const
{
    PlatformerAxisMoveResult result{};
    CollisionBodyComponent selfBody{};
    if (!entityGateway_.getResolvedCollisionBody(id, selfBody) || !selfBody.enabled)
        return result;

    const float dx = transform.position.x - beforeMove.position.x;
    if (std::abs(dx) > 1e-6f) {
        bool hitAny = false;
        PhysicsMath::SweepHit bestHit;
        EntityId hitEntity = INVALID_ENTITY;
        for (const CollisionWorld::ShapeRef& authoredSelf : collisionWorld_.shapes()) {
            if (authoredSelf.id != id)
                continue;
            if (!authoredSelf.shape.enabled
                || authoredSelf.shape.response != CollisionResponse::Solid)
                continue;
            if (!isPlatformerSolidRole(authoredSelf.shape.role))
                continue;

            CollisionWorld::ShapeRef moving = authoredSelf;
            moving.instance =
                CollisionWorld::shapeInstance(beforeMove, moving.shape);
            moving.aabb = PhysicsMath::shapeWorldAabb(moving.instance);

            const float selfHeight = moving.aabb.maxY - moving.aabb.minY;
            const float contactSlopY = platformerContactSlop(selfHeight);

            for (const CollisionWorld::ShapeRef& other : collisionWorld_.shapes()) {
                if (other.id == id)
                    continue;
                if (!other.shape.enabled
                    || other.shape.response != CollisionResponse::Solid)
                    continue;
                if (other.shape.oneWay)
                    continue; // ADR-0052: One Way never blocks X
                if (!CollisionWorld::canCollide(moving, other))
                    continue;

                const float overlapY =
                    std::min(moving.aabb.maxY, other.aabb.maxY)
                    - std::max(moving.aabb.minY, other.aabb.minY);
                if (overlapY <= contactSlopY)
                    continue;

                const PhysicsMath::SweepHit hit = PhysicsMath::sweepAabb(
                    moving.aabb, Vec2{ dx, 0.f }, other.aabb);
                if (!hit.hit || hit.fraction >= bestHit.fraction)
                    continue;
                bestHit = hit;
                hitAny = true;
                hitEntity = other.id;
            }
        }

        if (hitAny) {
            const float t = std::max(0.f, bestHit.fraction - 1e-4f);
            transform.position.x = beforeMove.position.x + dx * t;
            horizontalVelocity = 0.f;
            result.blocked = true;
            result.otherEntityId = hitEntity;
            if (dx > 0.f)
                result.hitPositive = true;
            else if (dx < 0.f)
                result.hitNegative = true;
        }
    }

    for (int pass = 0; pass < 4; ++pass) {
        bool resolvedAny = false;
        for (const CollisionWorld::ShapeRef& authoredSelf : collisionWorld_.shapes()) {
            if (authoredSelf.id != id)
                continue;
            if (!authoredSelf.shape.enabled
                || authoredSelf.shape.response != CollisionResponse::Solid)
                continue;
            if (!isPlatformerSolidRole(authoredSelf.shape.role))
                continue;

            CollisionWorld::ShapeRef selfRef = authoredSelf;
            selfRef.instance =
                CollisionWorld::shapeInstance(transform, selfRef.shape);
            selfRef.aabb = PhysicsMath::shapeWorldAabb(selfRef.instance);

            const float selfHeight = selfRef.aabb.maxY - selfRef.aabb.minY;
            const float contactSlopY = platformerContactSlop(selfHeight);

            for (const CollisionWorld::ShapeRef& other : collisionWorld_.shapes()) {
                if (resolvedAny || other.id == id)
                    break;
                if (!other.shape.enabled
                    || other.shape.response != CollisionResponse::Solid)
                    continue;
                if (other.shape.oneWay)
                    continue;
                if (!CollisionWorld::canCollide(selfRef, other))
                    continue;
                if (!PhysicsMath::aabbOverlap(selfRef.aabb, other.aabb))
                    continue;
                if (!PhysicsMath::shapesOverlap(selfRef.instance, other.instance))
                    continue;

                // ADR-0053: require meaningful orthogonal (Y) overlap for X recovery.
                const float overlapY =
                    std::min(selfRef.aabb.maxY, other.aabb.maxY)
                    - std::max(selfRef.aabb.minY, other.aabb.minY);
                if (overlapY <= contactSlopY)
                    continue;

                float correctionX = 0.f;
                if (!resolveAabbSeparationAxisX(selfRef.aabb, other.aabb, correctionX))
                    continue;
                transform.position.x += correctionX;
                horizontalVelocity = 0.f;
                result.blocked = true;
                result.otherEntityId = other.id;
                if (correctionX < 0.f)
                    result.hitPositive = true;
                else if (correctionX > 0.f)
                    result.hitNegative = true;
                resolvedAny = true;
            }
        }
        if (!resolvedAny)
            break;
    }
    return result;
}

PlatformerYMoveResult World::movePlatformerY(
    EntityId id,
    Transform& transform,
    const Transform& beforeY,
    float& verticalVelocity) const
{
    PlatformerYMoveResult result{};
    CollisionBodyComponent selfBody{};
    if (!entityGateway_.getResolvedCollisionBody(id, selfBody) || !selfBody.enabled)
        return result;

    const float dy = transform.position.y - beforeY.position.y;
    if (!std::isfinite(dy) || std::abs(dy) <= 1e-6f)
        return result;

    struct Candidate {
        float travel = 0.f;
        EntityId otherId = INVALID_ENTITY;
        std::size_t otherShapeIndex = 0;
        std::size_t selfShapeIndex = 0;
        bool floor = false;
        float surfaceY = 0.f;
        float selfExtentY = 0.f; // half-height unused; store before maxY/minY for place
        float selfBeforeEdge = 0.f;
    };
    std::optional<Candidate> best;

    for (const CollisionWorld::ShapeRef& authoredSelf : collisionWorld_.shapes()) {
        if (authoredSelf.id != id)
            continue;
        if (!authoredSelf.shape.enabled
            || authoredSelf.shape.response != CollisionResponse::Solid)
            continue;
        if (!isPlatformerSolidRole(authoredSelf.shape.role))
            continue;

        const PhysicsMath::Aabb selfBefore = PhysicsMath::shapeWorldAabb(
            CollisionWorld::shapeInstance(beforeY, authoredSelf.shape));
        Transform movedTransform = beforeY;
        movedTransform.position.y += dy;
        const PhysicsMath::Aabb selfMoved = PhysicsMath::shapeWorldAabb(
            CollisionWorld::shapeInstance(movedTransform, authoredSelf.shape));

        const float selfWidth = selfMoved.maxX - selfMoved.minX;
        const float contactSlop = platformerContactSlop(selfWidth);
        if (!std::isfinite(contactSlop))
            continue;

        CollisionWorld::ShapeRef selfRef = authoredSelf;
        selfRef.instance = CollisionWorld::shapeInstance(beforeY, authoredSelf.shape);
        selfRef.aabb = selfBefore;

        for (const CollisionWorld::ShapeRef& other : collisionWorld_.shapes()) {
            if (other.id == id)
                continue;
            if (!other.shape.enabled
                || other.shape.response != CollisionResponse::Solid)
                continue;
            if (!CollisionWorld::canCollide(selfRef, other))
                continue;

            const float overlapX =
                std::min(selfMoved.maxX, other.aabb.maxX)
                - std::max(selfMoved.minX, other.aabb.minX);
            if (overlapX <= contactSlop)
                continue; // side edge-touch: ignore for Y

            Candidate candidate;
            candidate.otherId = other.id;
            candidate.otherShapeIndex = other.shapeIndex;
            candidate.selfShapeIndex = authoredSelf.shapeIndex;

            if (dy > 0.f) {
                const float approach =
                    other.shape.oneWay ? kOneWayApproachTolerance : kGroundContactSkin;
                const bool fromAbove =
                    selfBefore.maxY <= other.aabb.minY + approach;
                const bool crossedTop =
                    selfMoved.maxY >= other.aabb.minY - kGroundContactSkin;
                if (!fromAbove || !crossedTop)
                    continue;
                const float travel = other.aabb.minY - selfBefore.maxY;
                if (travel < -kGroundContactSkin)
                    continue;
                candidate.travel = std::max(0.f, travel);
                candidate.floor = true;
                candidate.surfaceY = other.aabb.minY;
                candidate.selfBeforeEdge = selfBefore.maxY;
            } else {
                if (other.shape.oneWay)
                    continue;
                const bool fromBelow =
                    selfBefore.minY >= other.aabb.maxY - kGroundContactSkin;
                const bool crossedBottom =
                    selfMoved.minY <= other.aabb.maxY + kGroundContactSkin;
                if (!fromBelow || !crossedBottom)
                    continue;
                const float travel = selfBefore.minY - other.aabb.maxY;
                if (travel < -kGroundContactSkin)
                    continue;
                candidate.travel = std::max(0.f, travel);
                candidate.floor = false;
                candidate.surfaceY = other.aabb.maxY;
                candidate.selfBeforeEdge = selfBefore.minY;
            }

            const float fraction = candidate.travel / std::abs(dy);
            if (fraction > 1.f + 1e-3f)
                continue;

            const auto rank = [](const Candidate& c) {
                return std::tuple{
                    c.travel,
                    c.otherId,
                    c.otherShapeIndex,
                    c.selfShapeIndex,
                };
            };
            if (!best.has_value() || rank(candidate) < rank(*best))
                best = candidate;
        }
    }

    if (!best.has_value()) {
        // Keep requested Y displacement (already applied by caller).
        return result;
    }

    // Place flush against the first face along the motion; modify Y/vy only.
    if (best->floor) {
        const float feetBottomAtBefore = best->selfBeforeEdge;
        const float deltaToSurface = best->surfaceY - feetBottomAtBefore;
        transform.position.y = beforeY.position.y + deltaToSurface;
        verticalVelocity = 0.f;
        result.hitGround = true;
        result.contactEntityId = best->otherId;
    } else {
        const float headTopAtBefore = best->selfBeforeEdge;
        const float deltaToSurface = best->surfaceY - headTopAtBefore;
        transform.position.y = beforeY.position.y + deltaToSurface;
        verticalVelocity = 0.f;
        result.hitCeiling = true;
        result.contactEntityId = best->otherId;
    }
    return result;
}

void World::setRenderer(Modules::Renderer* renderer) {
    renderer_ = renderer;
    if (renderer_) cameraCenter_ = renderer_->getCameraCenter();
}

bool World::followCameraTarget(EntityId id) {
    Transform transform{};
    if (id == INVALID_ENTITY || !entityGateway_.getTransform(id, transform))
        return false;
    cameraFollowMode_ = CameraFollowMode::Explicit;
    cameraFollowTarget_ = id;
    return true;
}

void World::stopCameraFollow() {
    cameraFollowMode_ = CameraFollowMode::Disabled;
    cameraFollowTarget_ = INVALID_ENTITY;
}

void World::useAutomaticCameraTarget() {
    cameraFollowMode_ = CameraFollowMode::Automatic;
    cameraFollowTarget_ = INVALID_ENTITY;
}

void World::tickGameplaySystems(float dt) {
    if (const SceneDef* sc = entityGateway_.activeScene())
        activeTilemap_ = sc->tilemap;
    // Platformer runs in Application::tickFixedStep after Lua, before physics.step
    // (see FIXED_STEP_CONTRACT).
    tickTopDownControllers(dt);
    tickLinearMovers(dt);
    tickMagneticItems(dt);
    tickHordeMembers(dt);
    // Collision edge refresh intentionally runs after gameplay systems because
    // edges must be computed against fresh post-physics positions, so the
    // app driver invokes refreshCollisionEvents() after physics->step() and
    // syncPhysicsToEntities(). See Application::tickFixedStep.
}

void World::refreshCollisionEvents() {
    rebuildCollisionWorld();
    collisionEvents_ = collisionWorld_.refreshEvents();
}

void World::flushEntityQueues() {
    entityGateway_.flushPendingOperations();
}

void World::snapEntityToGrid(EntityId id, float cellSize) {
    if (cellSize <= 0.f) return;
    Transform transform{};
    if (!entityGateway_.getTransform(id, transform)) return;
    const float cs = cellSize;
    transform.position.x = std::round(transform.position.x / cs) * cs;
    transform.position.y = std::round(transform.position.y / cs) * cs;
    entityGateway_.setTransform(id, transform);
    if (const uint32_t handle = entityGateway_.physicsHandle(id); handle != 0)
        physics_.setPosition(handle, transform.position);
    rebuildCollisionWorld();
}

void World::moveEntityByOffset(EntityId id, float dx, float dy) {
    Transform transform{};
    if (!entityGateway_.getTransform(id, transform)) return;
    transform.position.x += dx;
    transform.position.y += dy;
    entityGateway_.setTransform(id, transform);
    if (const uint32_t handle = entityGateway_.physicsHandle(id); handle != 0)
        physics_.setPosition(handle, transform.position);
    rebuildCollisionWorld();
}

} // namespace ArtCade
