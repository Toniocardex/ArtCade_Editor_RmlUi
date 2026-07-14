#include "editor-native/model/project_document.h"
#include "editor-native/model/numeric_validation.h"
#include "editor-native/model/path_confinement.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {

ProjectDocument::ProjectDocument(ProjectDoc doc)
    : doc_(std::move(doc)) {}

const SceneDef* ProjectDocument::findScene(const SceneId& id) const {
    const auto it = doc_.scenes.find(id);
    return it == doc_.scenes.end() ? nullptr : &it->second;
}

bool ProjectDocument::hasScene(const SceneId& id) const {
    return doc_.scenes.find(id) != doc_.scenes.end();
}

bool ProjectDocument::hasImageAsset(const AssetId& id) const {
    return findImageAsset(id) != nullptr;
}

const ImageAssetDef* ProjectDocument::findImageAsset(const AssetId& id) const {
    for (const ImageAssetDef& asset : doc_.imageAssets) {
        if (asset.assetId == id) return &asset;
    }
    return nullptr;
}

bool ProjectDocument::hasSpriteAnimationAsset(const AssetId& id) const {
    return findSpriteAnimationAsset(id) != nullptr;
}

const SpriteAnimationAssetDef* ProjectDocument::findSpriteAnimationAsset(
    const AssetId& id) const {
    for (const SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id == id) return &asset;
    }
    return nullptr;
}

SceneDef* ProjectDocument::mutableScene(const SceneId& id) {
    const auto it = doc_.scenes.find(id);
    return it == doc_.scenes.end() ? nullptr : &it->second;
}

const SceneInstanceDef* ProjectDocument::findInstanceInScene(const SceneId& sceneId,
                                                             EntityId id) const {
    const SceneDef* scene = findScene(sceneId);
    if (!scene) return nullptr;
    for (const auto& instance : scene->instances) {
        if (instance.id == id) return &instance;
    }
    return nullptr;
}

bool ProjectDocument::hasObjectType(const std::string& id) const {
    return doc_.objectTypes.find(id) != doc_.objectTypes.end();
}

bool ProjectDocument::replaceLogicBoard(const std::string& objectTypeId,
                                        std::optional<LogicBoardDef> board) {
    const auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end()) return false;
    it->second.logicBoard = std::move(board);
    markDirty();
    return true;
}

const EntityDef* ProjectDocument::findObjectType(const std::string& id) const {
    const auto it = doc_.objectTypes.find(id);
    return it == doc_.objectTypes.end() ? nullptr : &it->second;
}

SceneInstanceDef* ProjectDocument::mutableInstanceInScene(const SceneId& sceneId,
                                                          EntityId id) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return nullptr;
    for (auto& instance : scene->instances) {
        if (instance.id == id) return &instance;
    }
    return nullptr;
}

void ProjectDocument::markDirty() {
    revision_ = ++revisionHighWater_;
}

void ProjectDocument::replace(ProjectDoc doc) {
    doc_ = std::move(doc);
    ++replaceCount_;
    markDirty();
}

void ProjectDocument::replaceClean(ProjectDocument replacement) {
    doc_ = std::move(replacement.doc_);
    ++replaceCount_;
    revision_ = ++revisionHighWater_;
    savedRevision_ = revision_;   // a freshly loaded document is clean
}

void ProjectDocument::markSaved() {
    savedRevision_ = revision_;
}

bool ProjectDocument::setProjectName(std::string name) {
    doc_.projectName = std::move(name);
    markDirty();
    return true;
}

bool ProjectDocument::setInstancePosition(const SceneId& sceneId, EntityId id, Vec2 position) {
    if (!NumericValidation::isFinite(position)) return false;
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance) return false;
    instance->transform.position = position;
    markDirty();
    return true;
}

bool ProjectDocument::setInstanceName(const SceneId& sceneId, EntityId id, std::string name) {
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance) return false;
    instance->instanceName = std::move(name);
    markDirty();
    return true;
}

bool ProjectDocument::setSceneName(const SceneId& sceneId, std::string name) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    scene->name = std::move(name);
    markDirty();
    return true;
}

bool ProjectDocument::setSceneSize(const SceneId& sceneId, Vec2 size) {
    if (!NumericValidation::isPositive(size)) return false;
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    scene->worldSize = size;   // instances are never moved (no clamp)
    markDirty();
    return true;
}

bool ProjectDocument::setSceneBackground(const SceneId& sceneId, Vec4 color) {
    if (!NumericValidation::isFinite(color)) return false;
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    scene->backgroundColor = color;
    markDirty();
    return true;
}

bool ProjectDocument::setStartSceneId(const SceneId& sceneId) {
    // startSceneId is the persisted doc.activeSceneId (legacy field name).
    if (!sceneId.empty() && !hasScene(sceneId)) return false;
    doc_.activeSceneId = sceneId;
    markDirty();
    return true;
}

bool ProjectDocument::createScene(const SceneId& id, const std::string& name) {
    if (id.empty() || hasScene(id)) return false;
    const bool firstScene = doc_.scenes.empty();
    SceneDef scene;
    scene.id = id;
    scene.name = name;
    // New scenes start on a dark neutral (#1e1e24) instead of the struct's
    // white: white blinds against the dark editor chrome and washes the grid
    // out. Authoring data as always - the Inspector can change it, and files
    // saved without the field keep deserializing to the struct default.
    scene.backgroundColor = Vec4{0.118f, 0.118f, 0.141f, 1.f};
    // Every scene always has a real first layer (the persistent fallback).
    scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1", false});
    scene.defaultLayerId = "layer-1";
    doc_.scenes.emplace(id, std::move(scene));
    if (firstScene) doc_.activeSceneId = id;
    markDirty();
    return true;
}

bool ProjectDocument::hasLayer(const SceneId& sceneId, const std::string& layerId) const {
    const SceneDef* scene = findScene(sceneId);
    if (!scene) return false;
    for (const SceneLayerDef& layer : scene->layers) {
        if (layer.id == layerId) return true;
    }
    return false;
}

bool ProjectDocument::isLayerLocked(const SceneId& sceneId, const std::string& layerId) const {
    const SceneDef* scene = findScene(sceneId);
    if (!scene) return false;
    for (const SceneLayerDef& layer : scene->layers) {
        if (layer.id == layerId) return layer.locked;
    }
    return false;
}

bool ProjectDocument::isInstanceLayerLocked(const SceneId& sceneId,
                                            const SceneInstanceDef& instance) const {
    return isLayerLocked(sceneId, effectiveLayerId(sceneId, instance));
}

std::string ProjectDocument::effectiveLayerId(const SceneId& sceneId,
                                              const SceneInstanceDef& instance) const {
    const SceneDef* scene = findScene(sceneId);
    if (!scene) return {};
    if (!instance.layerId.empty() && hasLayer(sceneId, instance.layerId)) return instance.layerId;
    return scene->defaultLayerId;
}

std::vector<const SceneInstanceDef*> ProjectDocument::instancesInRenderOrder(
    const SceneId& sceneId) const {
    std::vector<const SceneInstanceDef*> result;
    const SceneDef* scene = findScene(sceneId);
    if (!scene) return result;
    result.reserve(scene->instances.size());

    if (scene->layers.empty()) {
        // Legacy scene with no layers: keep the raw instance order.
        for (const SceneInstanceDef& inst : scene->instances) result.push_back(&inst);
        return result;
    }

    // Back-to-front: layers[0] is background, last is foreground.
    for (const SceneLayerDef& layer : scene->layers) {
        for (const SceneInstanceDef& inst : scene->instances) {
            if (effectiveLayerId(sceneId, inst) == layer.id) result.push_back(&inst);
        }
    }
    return result;
}

bool ProjectDocument::addSceneLayer(const SceneId& sceneId, const std::string& layerId,
                                    const std::string& name, std::size_t index) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene || layerId.empty() || name.empty()) return false;
    for (const SceneLayerDef& layer : scene->layers) {
        if (layer.id == layerId) return false;   // unique id
    }
    if (index > scene->layers.size()) index = scene->layers.size();
    scene->layers.insert(scene->layers.begin() + static_cast<std::ptrdiff_t>(index),
                         SceneLayerDef{layerId, name, false});
    markDirty();
    return true;
}

bool ProjectDocument::renameSceneLayer(const SceneId& sceneId, const std::string& layerId,
                                       const std::string& name) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene || name.empty()) return false;
    for (SceneLayerDef& layer : scene->layers) {
        if (layer.id == layerId) { layer.name = name; markDirty(); return true; }
    }
    return false;
}

bool ProjectDocument::setLayerLocked(const SceneId& sceneId, const std::string& layerId,
                                     bool locked) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    for (SceneLayerDef& layer : scene->layers) {
        if (layer.id == layerId) { layer.locked = locked; markDirty(); return true; }
    }
    return false;
}

bool ProjectDocument::moveSceneLayer(const SceneId& sceneId, const std::string& layerId,
                                     std::size_t index) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    std::size_t from = scene->layers.size();
    for (std::size_t i = 0; i < scene->layers.size(); ++i) {
        if (scene->layers[i].id == layerId) { from = i; break; }
    }
    if (from == scene->layers.size()) return false;
    if (index >= scene->layers.size()) index = scene->layers.size() - 1;
    if (index == from) return false;   // no-op
    const SceneLayerDef moved = scene->layers[from];
    scene->layers.erase(scene->layers.begin() + static_cast<std::ptrdiff_t>(from));
    scene->layers.insert(scene->layers.begin() + static_cast<std::ptrdiff_t>(index), moved);
    markDirty();
    return true;
}

bool ProjectDocument::removeSceneLayer(const SceneId& sceneId, const std::string& layerId) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    for (std::size_t i = 0; i < scene->layers.size(); ++i) {
        if (scene->layers[i].id == layerId) {
            scene->layers.erase(scene->layers.begin() + static_cast<std::ptrdiff_t>(i));
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::setInstanceLayer(const SceneId& sceneId, EntityId id,
                                       const std::string& layerId) {
    SceneInstanceDef* inst = mutableInstanceInScene(sceneId, id);
    if (!inst) return false;
    inst->layerId = layerId;
    markDirty();
    return true;
}

bool ProjectDocument::deleteScene(const SceneId& id) {
    const auto it = doc_.scenes.find(id);
    if (it == doc_.scenes.end()) return false;
    doc_.scenes.erase(it);
    if (doc_.activeSceneId == id) {
        doc_.activeSceneId = doc_.scenes.empty() ? SceneId{} : doc_.scenes.begin()->first;
    }
    markDirty();
    return true;
}

bool ProjectDocument::restoreScene(SceneDef scene, const SceneId& startSceneId) {
    if (scene.id.empty() || hasScene(scene.id)) return false;
    const SceneId id = scene.id;
    doc_.scenes.emplace(id, std::move(scene));
    doc_.activeSceneId = startSceneId;
    markDirty();
    return true;
}

bool ProjectDocument::createObjectType(EntityDef type) {
    if (type.className.empty()) return false;
    if (doc_.objectTypes.find(type.className) != doc_.objectTypes.end()) return false;
    doc_.objectTypes.emplace(type.className, std::move(type));
    markDirty();
    return true;
}

bool ProjectDocument::removeObjectType(const std::string& id) {
    const auto it = doc_.objectTypes.find(id);
    if (it == doc_.objectTypes.end()) return false;
    doc_.objectTypes.erase(it);
    markDirty();
    return true;
}

bool ProjectDocument::setObjectTypeName(const std::string& id, std::string name) {
    const auto it = doc_.objectTypes.find(id);
    if (it == doc_.objectTypes.end()) return false;
    it->second.name = std::move(name);
    markDirty();
    return true;
}

bool ProjectDocument::createInstance(const SceneId& sceneId, SceneInstanceDef instance) {
    if (!NumericValidation::isFinite(instance.transform)) return false;
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    for (const auto& existing : scene->instances) {
        if (existing.id == instance.id) return false; // id unique within scene
    }
    scene->instances.push_back(std::move(instance));
    markDirty();
    return true;
}

bool ProjectDocument::insertInstance(const SceneId& sceneId, std::size_t index,
                                     SceneInstanceDef instance) {
    if (!NumericValidation::isFinite(instance.transform)) return false;
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    for (const auto& existing : scene->instances) {
        if (existing.id == instance.id) return false;
    }
    const std::size_t clamped = std::min(index, scene->instances.size());
    scene->instances.insert(scene->instances.begin() + static_cast<std::ptrdiff_t>(clamped),
                            std::move(instance));
    markDirty();
    return true;
}

bool ProjectDocument::deleteInstance(const SceneId& sceneId, EntityId id) {
    SceneDef* scene = mutableScene(sceneId);
    if (!scene) return false;
    for (auto it = scene->instances.begin(); it != scene->instances.end(); ++it) {
        if (it->id == id) {
            scene->instances.erase(it);
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::addTilemapComponent(const SceneId& sceneId, EntityId id,
                                          TilemapComponent component) {
    if (!NumericValidation::isValid(component)) return false;
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance || instance->tilemap.has_value()) return false;
    instance->tilemap = std::move(component);
    markDirty();
    return true;
}

bool ProjectDocument::removeTilemapComponent(const SceneId& sceneId, EntityId id) {
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance || !instance->tilemap.has_value()) return false;
    instance->tilemap.reset();
    markDirty();
    return true;
}

bool ProjectDocument::setTilemapTileset(const SceneId& sceneId, EntityId id,
                                        AssetId tilesetAssetId) {
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance || !instance->tilemap.has_value()) return false;
    instance->tilemap->tilesetAssetId = std::move(tilesetAssetId);
    markDirty();
    return true;
}

bool ProjectDocument::setTilemapCellSize(const SceneId& sceneId, EntityId id, Vec2 cellSize) {
    if (!NumericValidation::isPositive(cellSize)) return false;
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance || !instance->tilemap.has_value()) return false;
    instance->tilemap->cellSize = cellSize;
    markDirty();
    return true;
}

bool ProjectDocument::setTilemapComponent(const SceneId& sceneId, EntityId id,
                                          TilemapComponent replacement) {
    if (!NumericValidation::isValid(replacement)) return false;
    SceneInstanceDef* instance = mutableInstanceInScene(sceneId, id);
    if (!instance || !instance->tilemap.has_value()) return false;
    instance->tilemap = std::move(replacement);
    markDirty();
    return true;
}

bool ProjectDocument::addBoxCollider(const std::string& objectTypeId,
                                     BoxCollider2DComponent component) {
    if (!NumericValidation::isValid(component)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || it->second.boxCollider2D.has_value()) return false;
    it->second.boxCollider2D = component;
    markDirty();
    return true;
}

bool ProjectDocument::removeBoxCollider(const std::string& objectTypeId) {
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.boxCollider2D.has_value()) return false;
    it->second.boxCollider2D.reset();
    markDirty();
    return true;
}

bool ProjectDocument::setBoxColliderOffset(const std::string& objectTypeId, Vec2 offset) {
    if (!NumericValidation::isFinite(offset)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.boxCollider2D.has_value()) return false;
    it->second.boxCollider2D->offset = offset;
    markDirty();
    return true;
}

bool ProjectDocument::setBoxColliderSize(const std::string& objectTypeId, Vec2 size) {
    if (!NumericValidation::isPositive(size)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.boxCollider2D.has_value()) return false;
    it->second.boxCollider2D->size = size;
    markDirty();
    return true;
}

bool ProjectDocument::setBoxColliderEnabled(const std::string& objectTypeId, bool enabled) {
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.boxCollider2D.has_value()) return false;
    it->second.boxCollider2D->enabled = enabled;
    markDirty();
    return true;
}

bool ProjectDocument::setBoxColliderMode(const std::string& objectTypeId, BoxColliderMode mode) {
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.boxCollider2D.has_value()) return false;
    it->second.boxCollider2D->mode = mode;
    markDirty();
    return true;
}

bool ProjectDocument::addLinearMover(const std::string& objectTypeId,
                                     LinearMoverComponent component) {
    if (!NumericValidation::isValid(component)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || it->second.linearMover.has_value()) return false;
    it->second.linearMover = component;
    markDirty();
    return true;
}

bool ProjectDocument::removeLinearMover(const std::string& objectTypeId) {
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.linearMover.has_value()) return false;
    it->second.linearMover.reset();
    markDirty();
    return true;
}

bool ProjectDocument::setLinearMoverDirection(const std::string& objectTypeId, Vec2 direction) {
    if (!NumericValidation::isFinite(direction)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.linearMover.has_value()) return false;
    it->second.linearMover->directionX = direction.x;
    it->second.linearMover->directionY = direction.y;
    markDirty();
    return true;
}

bool ProjectDocument::setLinearMoverSpeed(const std::string& objectTypeId, float speed) {
    if (!NumericValidation::isNonNegative(speed)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.linearMover.has_value()) return false;
    it->second.linearMover->speed = speed;
    markDirty();
    return true;
}

bool ProjectDocument::addTopDownController(const std::string& objectTypeId,
                                          TopDownControllerComponent component) {
    if (!NumericValidation::isValid(component)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || it->second.topDownController.has_value()) return false;
    it->second.topDownController = component;
    markDirty();
    return true;
}

bool ProjectDocument::removeTopDownController(const std::string& objectTypeId) {
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.topDownController.has_value()) return false;
    it->second.topDownController.reset();
    markDirty();
    return true;
}

bool ProjectDocument::setTopDownControllerSpeed(const std::string& objectTypeId, float speed) {
    if (!NumericValidation::isNonNegative(speed)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.topDownController.has_value()) return false;
    it->second.topDownController->maxSpeed = speed;
    markDirty();
    return true;
}

bool ProjectDocument::addPlatformerController(const std::string& objectTypeId,
                                             PlatformerControllerComponent component) {
    if (!NumericValidation::isValid(component)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || it->second.platformerController.has_value()) return false;
    it->second.platformerController = component;
    markDirty();
    return true;
}

bool ProjectDocument::removePlatformerController(const std::string& objectTypeId) {
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.platformerController.has_value()) return false;
    it->second.platformerController.reset();
    markDirty();
    return true;
}

bool ProjectDocument::setPlatformerValue(const std::string& objectTypeId, int field, float value) {
    if (!NumericValidation::isNonNegative(value)) return false;
    auto it = doc_.objectTypes.find(objectTypeId);
    if (it == doc_.objectTypes.end() || !it->second.platformerController.has_value()) return false;
    PlatformerControllerComponent& pc = *it->second.platformerController;
    switch (field) {       // matches commands/PlatformerField
        case 0: pc.maxSpeed      = value; break;
        case 1: pc.jumpForce     = value; break;
        case 2: pc.customGravity = value; break;
        default: return false;
    }
    markDirty();
    return true;
}

bool ProjectDocument::addImageAsset(ImageAssetDef asset) {
    if (asset.assetId.empty() || hasImageAsset(asset.assetId)
        || !isSafeProjectRelativePath(std::filesystem::u8path(asset.sourcePath))) return false;
    doc_.imageAssets.push_back(std::move(asset));
    markDirty();
    return true;
}

bool ProjectDocument::removeImageAsset(const AssetId& assetId) {
    for (auto it = doc_.imageAssets.begin(); it != doc_.imageAssets.end(); ++it) {
        if (it->assetId == assetId) {
            doc_.imageAssets.erase(it);
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::addSpriteAnimationAsset(SpriteAnimationAssetDef asset) {
    if (asset.id.empty() || hasSpriteAnimationAsset(asset.id)) {
        return false;
    }
    doc_.spriteAnimationAssets.push_back(std::move(asset));
    markDirty();
    return true;
}

bool ProjectDocument::removeSpriteAnimationAsset(const AssetId& assetId) {
    for (auto it = doc_.spriteAnimationAssets.begin();
         it != doc_.spriteAnimationAssets.end(); ++it) {
        if (it->id == assetId) {
            doc_.spriteAnimationAssets.erase(it);
            markDirty();
            return true;
        }
    }
    return false;
}

void ProjectDocument::commitStagedCommand(ProjectDoc staged) {
    using std::swap;
    swap(doc_, staged);
    markDirty();
}

bool ProjectDocument::addAnimationClip(const AssetId& assetId,
                                       SpriteAnimationClipDef clip) {
    if (!NumericValidation::isPositive(clip.framesPerSecond)) return false;
    for (SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id != assetId) continue;
        if (clip.id.empty() || clip.name.empty()) return false;
        for (const SpriteAnimationClipDef& existing : asset.clips) {
            if (existing.id == clip.id || existing.name == clip.name) return false;
        }
        asset.clips.push_back(std::move(clip));
        // The first clip becomes the asset's default (what entities play).
        if (asset.clips.size() == 1) asset.defaultClipId = asset.clips.front().id;
        markDirty();
        return true;
    }
    return false;
}

bool ProjectDocument::renameAnimationClip(const AssetId& assetId,
                                          const std::string& clipId,
                                          std::string name) {
    if (name.empty()) return false;
    for (SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id != assetId) continue;
        for (const SpriteAnimationClipDef& existing : asset.clips) {
            if (existing.id != clipId && existing.name == name) return false;
        }
        for (SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == clipId) {
                clip.name = std::move(name);
                markDirty();
                return true;
            }
        }
    }
    return false;
}

bool ProjectDocument::removeAnimationClip(const AssetId& assetId,
                                          const std::string& clipId) {
    for (SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id != assetId) continue;
        for (auto it = asset.clips.begin(); it != asset.clips.end(); ++it) {
            if (it->id == clipId) {
                asset.clips.erase(it);
                // Keep the default pointing at a real clip (first remaining).
                if (asset.defaultClipId == clipId) {
                    asset.defaultClipId = asset.clips.empty()
                        ? std::string() : asset.clips.front().id;
                }
                markDirty();
                return true;
            }
        }
    }
    return false;
}

bool ProjectDocument::setAnimationClipFrames(
    const AssetId& assetId, const std::string& clipId,
    std::vector<SpriteAnimationFrameDef> frames) {
    for (SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id != assetId) continue;
        for (SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == clipId) {
                clip.frames = std::move(frames);
                markDirty();
                return true;
            }
        }
    }
    return false;
}

bool ProjectDocument::setAnimationClipFrameRate(const AssetId& assetId,
                                                const std::string& clipId,
                                                float fps) {
    if (!NumericValidation::isPositive(fps)) return false;
    for (SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id != assetId) continue;
        for (SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == clipId) {
                clip.framesPerSecond = fps;
                markDirty();
                return true;
            }
        }
    }
    return false;
}

bool ProjectDocument::setAnimationClipPlaybackMode(const AssetId& assetId,
                                                   const std::string& clipId,
                                                   AnimationPlaybackMode mode) {
    for (SpriteAnimationAssetDef& asset : doc_.spriteAnimationAssets) {
        if (asset.id != assetId) continue;
        for (SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == clipId) {
                clip.playbackMode = mode;
                markDirty();
                return true;
            }
        }
    }
    return false;
}

bool ProjectDocument::hasAudioAsset(const AssetId& id) const {
    return findAudioAsset(id) != nullptr;
}

const AudioAssetDef* ProjectDocument::findAudioAsset(const AssetId& id) const {
    for (const AudioAssetDef& asset : doc_.audioAssets) {
        if (asset.assetId == id) return &asset;
    }
    return nullptr;
}

bool ProjectDocument::addAudioAsset(AudioAssetDef asset) {
    if (asset.assetId.empty() || hasAudioAsset(asset.assetId)
        || !isSafeProjectRelativePath(std::filesystem::u8path(asset.sourcePath))) return false;
    doc_.audioAssets.push_back(std::move(asset));
    markDirty();
    return true;
}

bool ProjectDocument::removeAudioAsset(const AssetId& assetId) {
    for (auto it = doc_.audioAssets.begin(); it != doc_.audioAssets.end(); ++it) {
        if (it->assetId == assetId) {
            doc_.audioAssets.erase(it);
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::hasFontAsset(const AssetId& id) const {
    return findFontAsset(id) != nullptr;
}

const FontAssetDef* ProjectDocument::findFontAsset(const AssetId& id) const {
    for (const FontAssetDef& asset : doc_.fontAssets) {
        if (asset.assetId == id) return &asset;
    }
    return nullptr;
}

bool ProjectDocument::addFontAsset(FontAssetDef asset) {
    if (asset.assetId.empty() || hasFontAsset(asset.assetId)
        || !isSafeProjectRelativePath(std::filesystem::u8path(asset.sourcePath))) return false;
    doc_.fontAssets.push_back(std::move(asset));
    markDirty();
    return true;
}

bool ProjectDocument::removeFontAsset(const AssetId& assetId) {
    for (auto it = doc_.fontAssets.begin(); it != doc_.fontAssets.end(); ++it) {
        if (it->assetId == assetId) {
            doc_.fontAssets.erase(it);
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::hasTilesetAsset(const AssetId& id) const {
    return findTilesetAsset(id) != nullptr;
}

const TilesetAsset* ProjectDocument::findTilesetAsset(const AssetId& id) const {
    for (const TilesetAsset& asset : doc_.tilesets) {
        if (asset.assetId == id) return &asset;
    }
    return nullptr;
}

bool ProjectDocument::addTilesetAsset(TilesetAsset asset) {
    if (asset.assetId.empty() || hasTilesetAsset(asset.assetId)) return false;
    doc_.tilesets.push_back(std::move(asset));
    markDirty();
    return true;
}

bool ProjectDocument::removeTilesetAsset(const AssetId& assetId) {
    for (auto it = doc_.tilesets.begin(); it != doc_.tilesets.end(); ++it) {
        if (it->assetId == assetId) {
            doc_.tilesets.erase(it);
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::setTilesetName(const AssetId& assetId, std::string name) {
    for (TilesetAsset& asset : doc_.tilesets) {
        if (asset.assetId == assetId) {
            asset.name = std::move(name);
            markDirty();
            return true;
        }
    }
    return false;
}

bool ProjectDocument::setTilesetSlicing(const AssetId& assetId, TilesetSlicing slicing,
                                       std::vector<TileDefinition> tiles) {
    for (TilesetAsset& asset : doc_.tilesets) {
        if (asset.assetId == assetId) {
            asset.slicing = slicing;
            asset.tiles   = std::move(tiles);
            markDirty();
            return true;
        }
    }
    return false;
}

} // namespace ArtCade::EditorNative
