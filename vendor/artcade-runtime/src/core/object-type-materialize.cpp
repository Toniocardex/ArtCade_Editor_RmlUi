#include "object-type-materialize.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace ArtCade {

namespace {

AssetId resolveAnimationSourceImage(
    const AnimationAssetId& animationAssetId,
    const std::vector<SpriteAnimationAssetDef>& animationAssets) {
    if (animationAssetId.empty()) return {};
    for (const SpriteAnimationAssetDef& asset : animationAssets) {
        if (asset.id == animationAssetId) return asset.sourceImageAssetId;
    }
    return {};
}

bool finitePositiveSize(const Vec2& size) {
    return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0.f && size.y > 0.f;
}

} // namespace

BodyType resolveCollisionBodyType(const EntityDef& objectType) {
    if (objectType.platformerController.has_value()
        || objectType.topDownController.has_value()
        || objectType.linearMover.has_value()) {
        return BodyType::Kinematic;
    }
    return BodyType::Static;
}

std::optional<CollisionBodyComponent> materializeBoxCollider2D(const EntityDef& objectType) {
    if (!objectType.boxCollider2D.has_value()) return std::nullopt;
    const BoxCollider2DComponent& bc = *objectType.boxCollider2D;
    if (!bc.enabled) return std::nullopt;
    if (!std::isfinite(bc.offset.x) || !std::isfinite(bc.offset.y)
        || !finitePositiveSize(bc.size)) {
        return std::nullopt;
    }

    CollisionShape shape;
    shape.type = CollisionShapeType::Rectangle;
    shape.role = CollisionShapeRole::Body;
    shape.offset = bc.offset;
    shape.size = bc.size;
    shape.enabled = true;
    shape.layerId = "default";
    shape.maskLayerIds = { "default" };

    switch (bc.mode) {
        case BoxColliderMode::Solid:
            shape.response = CollisionResponse::Solid;
            shape.oneWay = false;
            break;
        case BoxColliderMode::Trigger:
            shape.response = CollisionResponse::Sensor;
            shape.oneWay = false;
            break;
        case BoxColliderMode::OneWayPlatform:
            shape.response = CollisionResponse::Solid;
            shape.oneWay = true;
            break;
    }

    CollisionBodyComponent body;
    body.enabled = true;
    body.bodyType = resolveCollisionBodyType(objectType);
    body.shapes = { std::move(shape) };
    return body;
}

EntityDef materializeInstance(
    const EntityDef& typeProto,
    const SceneInstanceDef& instance,
    const std::vector<SpriteAnimationAssetDef>& animationAssets) {
    EntityDef e = typeProto;
    e.id        = instance.id;
    e.className = typeProto.className.empty() ? instance.objectTypeId : typeProto.className;
    e.name      = instance.instanceName.empty() ? typeProto.name : instance.instanceName;
    e.transform = instance.transform;
    e.visible   = instance.visible;
    e.layerId   = instance.layerId;
    e.localVariableOverrides = instance.localVariableOverrides;
    // Camera target is scene-instance authority (ADR-0003), so it replaces
    // the compatibility-only type-level field during materialisation.
    e.cameraTarget = instance.cameraTarget;
    if (e.spritePresentation) {
        SpritePresentationComponent presentation = *e.spritePresentation;
        if (instance.spritePresentationOverride) {
            const SpritePresentationOverride& delta = *instance.spritePresentationOverride;
            if (delta.visible) presentation.visible = *delta.visible;
            if (delta.source) presentation.source = *delta.source;
        }
        SpriteRendererComponent renderer;
        renderer.visible = presentation.visible;
        e.spriteAnimator.reset();
        if (const auto* image = std::get_if<SpritePresentationImage>(&presentation.source)) {
            renderer.imageAssetId = image->imageAssetId;
        } else if (const auto* animation =
                       std::get_if<SpritePresentationAnimation>(&presentation.source)) {
            // ADR-0010: Animation already names its draw sheet on the asset.
            // Populate the runtime renderer here so Play/export do not depend
            // on defaultClipId or maybePlaySpawnClip for a drawable sheet.
            renderer.imageAssetId =
                resolveAnimationSourceImage(animation->animationAssetId, animationAssets);
            e.spriteAnimator = SpriteAnimatorComponent{
                animation->animationAssetId, animation->defaultClipId,
                animation->autoPlay, animation->playbackSpeed};
        }
        e.spriteRenderer = std::move(renderer);
        e.spritePresentation.reset();
    }
    if (instance.spriteRendererOverride) {
        const SpriteRendererOverride& delta = *instance.spriteRendererOverride;
        if (delta.capabilityEnabled && !*delta.capabilityEnabled) {
            e.spriteRenderer.reset();
        } else if (e.spriteRenderer) {
            if (delta.imageAssetId) e.spriteRenderer->imageAssetId = *delta.imageAssetId;
            if (delta.visible) e.spriteRenderer->visible = *delta.visible;
        }
    }
    if (instance.spriteAnimatorOverride) {
        const SpriteAnimatorOverride& delta = *instance.spriteAnimatorOverride;
        if (delta.capabilityEnabled && !*delta.capabilityEnabled) {
            e.spriteAnimator.reset();
        } else if (e.spriteAnimator) {
            if (delta.animationAssetId)
                e.spriteAnimator->animationAssetId = *delta.animationAssetId;
            if (delta.defaultClipId)
                e.spriteAnimator->defaultClipId = *delta.defaultClipId;
            if (delta.autoPlay) e.spriteAnimator->autoPlay = *delta.autoPlay;
            if (delta.playbackSpeed) e.spriteAnimator->playbackSpeed = *delta.playbackSpeed;
        }
    }
    if (e.spriteRenderer) {
        e.visible = e.visible && e.spriteRenderer->visible;
        if (!e.spriteRenderer->imageAssetId.empty())
            e.sprite.spriteAssetId = e.spriteRenderer->imageAssetId;
    }
    // ADR-0014: CollisionBody is session scratch derived from BoxCollider2D only.
    // Absent / disabled / invalid box → clear any leftover authored body.
    e.collisionBody = materializeBoxCollider2D(e);
    return e;
}

void materializeProjectEntities(ProjectDoc& doc) {
    if (doc.objectTypes.empty()) return;

    const bool hasInstances = std::any_of(
        doc.scenes.begin(), doc.scenes.end(),
        [](const auto& kv) { return !kv.second.instances.empty(); });
    if (!hasInstances && !doc.entities.empty()) return;

    // The editor always ships `entities` as a derived cache next to
    // objectTypes+instances, so overlapping ids are the normal case. Only warn
    // about entity ids no instance accounts for — those are true legacy leftovers.
    if (hasInstances && !doc.entities.empty()) {
        std::unordered_set<EntityId> instanceIds;
        for (const auto& [sid, scene] : doc.scenes) {
            (void)sid;
            for (const SceneInstanceDef& inst : scene.instances)
                instanceIds.insert(inst.id);
        }
        size_t legacyOnly = 0;
        for (const auto& [id, def] : doc.entities) {
            (void)def;
            if (instanceIds.find(id) == instanceIds.end())
                ++legacyOnly;
        }
        if (legacyOnly > 0) {
            std::cerr << "[Project] Warning: " << legacyOnly
                      << " legacy entity definition(s) have no matching scene "
                         "instance; they will be kept as-is while instance IDs "
                         "override matching entries.\n";
        }
    }

    for (auto& [sid, scene] : doc.scenes) {
        if (scene.instances.empty()) continue;
        scene.entityIds.clear();
        for (const SceneInstanceDef& inst : scene.instances) {
            auto typeIt = doc.objectTypes.find(inst.objectTypeId);
            if (typeIt == doc.objectTypes.end()) {
                std::cerr << "[Project] Unknown objectTypeId \"" << inst.objectTypeId
                          << "\" for instance id " << inst.id << " — skipped.\n";
                continue;
            }
            EntityDef e =
                materializeInstance(typeIt->second, inst, doc.spriteAnimationAssets);
            doc.entities[e.id] = e;
            scene.entityIds.push_back(e.id);
        }
        (void)sid;
    }
}

void rebuildClassPrototypes(
    std::unordered_map<std::string, EntityDef>& out,
    const std::unordered_map<std::string, EntityDef>& objectTypes,
    const std::unordered_map<EntityId, EntityDef>& entityDefs)
{
    out.clear();
    for (const auto& [typeId, def] : objectTypes) {
        EntityDef copy = def;
        copy.className = typeId;
        // ADR-0014: spawn templates carry the derived body so spawnFromClass
        // matches scene-load materialisation.
        copy.collisionBody = materializeBoxCollider2D(copy);
        out[typeId] = std::move(copy);
    }
    for (const auto& [id, def] : entityDefs) {
        (void)id;
        if (def.className.empty()) continue;
        if (out.find(def.className) == out.end()) {
            EntityDef copy = def;
            copy.collisionBody = materializeBoxCollider2D(copy);
            out[def.className] = std::move(copy);
        }
    }
}

void resolveSpritePivotsFromImageAssets(ProjectDoc& doc) {
    if (doc.imageAssets.empty()) return;

    std::unordered_map<std::string, Vec2> pivotByPath;
    for (const ImageAssetDef& ad : doc.imageAssets)
        pivotByPath[ad.assetId] = ad.defaultPivot;

    const auto apply = [&](EntityDef& e) {
        if (!e.sprite.pivotFromAsset) return;
        const auto it = pivotByPath.find(e.sprite.spriteAssetId);
        e.sprite.pivot = it != pivotByPath.end() ? it->second : Vec2{0.5f, 0.5f};
    };

    for (auto& [_, e] : doc.entities)
        apply(e);
    for (auto& [_, t] : doc.objectTypes)
        apply(t);
}

} // namespace ArtCade
