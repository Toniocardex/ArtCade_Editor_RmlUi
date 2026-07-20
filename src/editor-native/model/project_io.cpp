#include "editor-native/model/project_io.h"
#include "artcade/sfx/recipe_json.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "logic-core.h"
#include "sprite-animation-core.h"
#include "editor-native/model/generated_sfx_policy.h"
#include "editor-native/model/numeric_validation.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/sprite_render_view.h"

#include "editor-native/model/tilemap_validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ArtCade::EditorNative {

namespace {

// v8: Generated SFX has one-way output ownership. Linked AudioAssetDef entries
// carry no mirrored display name; provenance is derived and validated.
// v7: Object-Type-owned ordered Script attachments. Project JSON still stores
// metadata only; source text remains in project-relative .lua files.
constexpr int kCurrentSchemaVersion = 9;

class JsonReadError final : public std::runtime_error {
public:
    explicit JsonReadError(std::string message) : std::runtime_error(std::move(message)) {}
};

[[noreturn]] void invalidField(const std::string& field, const char* expected) {
    throw JsonReadError("Invalid project field '" + field + "': expected " + expected);
}

const nlohmann::json* findMember(const nlohmann::json& object, const char* camel,
                                 const char* snake = nullptr) {
    if (const auto it = object.find(camel); it != object.end()) return &*it;
    if (snake) {
        if (const auto it = object.find(snake); it != object.end()) return &*it;
    }
    return nullptr;
}

const nlohmann::json& requireObject(const nlohmann::json& value,
                                    const std::string& field) {
    if (!value.is_object()) invalidField(field, "object");
    return value;
}

const nlohmann::json* optionalObject(const nlohmann::json& object, const char* key,
                                     const std::string& field) {
    const auto it = object.find(key);
    if (it == object.end()) return nullptr;
    if (!it->is_object()) invalidField(field, "object");
    return &*it;
}

const nlohmann::json* optionalArray(const nlohmann::json& object, const char* key,
                                    const std::string& field) {
    const auto it = object.find(key);
    if (it == object.end()) return nullptr;
    if (!it->is_array()) invalidField(field, "array");
    return &*it;
}

template <class T>
T readNumber(const nlohmann::json& object, const char* key, T fallback,
             const std::string& field) {
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    const bool numeric = [&] {
        if constexpr (std::is_integral_v<T>) {
            return it->is_number_integer() || it->is_number_unsigned();
        }
        return it->is_number();
    }();
    if (!numeric) invalidField(field, std::is_integral_v<T> ? "integer" : "number");
    try {
        if constexpr (std::is_integral_v<T>) {
            if (it->is_number_unsigned()) {
                const std::uint64_t value = it->get<std::uint64_t>();
                if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                    throw JsonReadError(
                        "Invalid project field '" + field + "': number is out of range");
                }
                return static_cast<T>(value);
            }
            const std::int64_t value = it->get<std::int64_t>();
            if constexpr (std::is_unsigned_v<T>) {
                if (value < 0
                    || static_cast<std::uint64_t>(value)
                           > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                    throw JsonReadError(
                        "Invalid project field '" + field + "': number is out of range");
                }
            } else if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min())
                       || value > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                throw JsonReadError(
                    "Invalid project field '" + field + "': number is out of range");
            }
            return static_cast<T>(value);
        } else {
            const T value = it->get<T>();
            if (!std::isfinite(value)) {
                throw JsonReadError(
                    "Invalid project field '" + field + "': number is out of range");
            }
            return value;
        }
    } catch (const JsonReadError&) {
        throw;
    } catch (const nlohmann::json::exception&) {
        throw JsonReadError("Invalid project field '" + field + "': number is out of range");
    }
}

template <class T>
T readAliasedNumber(const nlohmann::json& object, const char* camel, const char* snake,
                    T fallback, const std::string& field) {
    if (object.contains(camel)) return readNumber<T>(object, camel, fallback, field);
    return readNumber<T>(object, snake, fallback, field);
}

bool readBool(const nlohmann::json& object, const char* key, bool fallback,
              const std::string& field) {
    const auto it = object.find(key);
    if (it == object.end()) return fallback;
    if (!it->is_boolean()) invalidField(field, "boolean");
    return it->get<bool>();
}

nlohmann::json vec2ToJson(const Vec2& v) {
    return nlohmann::json{{"x", v.x}, {"y", v.y}};
}

nlohmann::json vec3ToJson(const Vec3& v) {
    return nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

std::string readString(const nlohmann::json& object, const char* camel,
                       const char* snake = nullptr,
                       const std::string& fallback = {}) {
    const nlohmann::json* value = findMember(object, camel, snake);
    if (!value) return fallback;
    if (!value->is_string()) invalidField(camel, "string");
    return value->get<std::string>();
}

float readFloat(const nlohmann::json& object, const char* key, float fallback) {
    return readNumber<float>(object, key, fallback, key);
}

Vec2 readVec2(const nlohmann::json& value, Vec2 fallback = {}) {
    requireObject(value, "Vec2");
    return Vec2{
        readFloat(value, "x", fallback.x),
        readFloat(value, "y", fallback.y),
    };
}

const char* BoxColliderModeToString(BoxColliderMode mode) {
    switch (mode) {
        case BoxColliderMode::Solid: return "solid";
        case BoxColliderMode::Trigger: return "trigger";
        case BoxColliderMode::OneWayPlatform: return "oneWayPlatform";
    }
    return "solid";
}

BoxColliderMode legacyTriggerMode(bool isTrigger) {
    return isTrigger ? BoxColliderMode::Trigger : BoxColliderMode::Solid;
}

std::optional<BoxColliderMode> BoxColliderModeFromString(const std::string& value) {
    if (value == "solid") return BoxColliderMode::Solid;
    if (value == "trigger") return BoxColliderMode::Trigger;
    if (value == "oneWayPlatform") return BoxColliderMode::OneWayPlatform;
    return std::nullopt;
}

const char* playbackModeToString(AnimationPlaybackMode mode) {
    switch (mode) {
        case AnimationPlaybackMode::Loop: return "loop";
        case AnimationPlaybackMode::Once: return "once";
    }
    return "loop";
}

std::optional<AnimationPlaybackMode> playbackModeFromString(const std::string& value) {
    if (value == "loop") return AnimationPlaybackMode::Loop;
    if (value == "once") return AnimationPlaybackMode::Once;
    return std::nullopt;
}

std::string readDefaultClipId(const nlohmann::json& object) {
    if (findMember(object, "defaultClipId", "default_clip_id")) {
        return readString(object, "defaultClipId", "default_clip_id");
    }
    return readString(object, "initialClipId", "initial_clip_id");
}

std::string sanitizeImageIdFragment(const std::string& imageId) {
    std::string out;
    out.reserve(imageId.size());
    for (unsigned char c : imageId) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? std::string("image") : out;
}

bool assetOwnsClip(const SpriteAnimationAssetDef& asset, const std::string& clipId) {
    return std::any_of(asset.clips.begin(), asset.clips.end(),
        [&](const SpriteAnimationClipDef& clip) { return clip.id == clipId; });
}

const SpriteAnimationAssetDef* findAnimationAsset(const ProjectDoc& document,
                                                  const AssetId& assetId) {
    for (const SpriteAnimationAssetDef& asset : document.spriteAnimationAssets) {
        if (asset.id == assetId) return &asset;
    }
    return nullptr;
}

AssetId resolveAnimationAssetForClip(const ProjectDoc& document,
                                     const AssetId& animationAssetId,
                                     const std::string& clipId) {
    if (animationAssetId.empty() || clipId.empty()) return animationAssetId;
    if (const SpriteAnimationAssetDef* current =
            findAnimationAsset(document, animationAssetId)) {
        if (assetOwnsClip(*current, clipId)) return animationAssetId;
    }
    const std::string splitPrefix = animationAssetId + "-img-";
    for (const SpriteAnimationAssetDef& asset : document.spriteAnimationAssets) {
        if (!assetOwnsClip(asset, clipId)) continue;
        if (asset.id == animationAssetId || asset.id.rfind(splitPrefix, 0) == 0) {
            return asset.id;
        }
    }
    // No global first-wins: keep the authored id so validation can reject it.
    return animationAssetId;
}

const char* audioLoadModeToString(AudioLoadMode mode) {
    switch (mode) {
        case AudioLoadMode::StaticSound: return "static";
        case AudioLoadMode::Stream: return "stream";
    }
    return "static";
}

std::optional<AudioLoadMode> audioLoadModeFromString(const std::string& value) {
    if (value == "static") return AudioLoadMode::StaticSound;
    if (value == "stream") return AudioLoadMode::Stream;
    return std::nullopt;
}

const char* fontGlyphPresetToString(FontGlyphPreset preset) {
    switch (preset) {
        case FontGlyphPreset::BasicLatin: return "basicLatin";
        case FontGlyphPreset::European: return "european";
        case FontGlyphPreset::CustomText: return "customText";
    }
    return "european";
}

std::optional<FontGlyphPreset> fontGlyphPresetFromString(const std::string& value) {
    if (value == "basicLatin") return FontGlyphPreset::BasicLatin;
    if (value == "european") return FontGlyphPreset::European;
    if (value == "customText") return FontGlyphPreset::CustomText;
    return std::nullopt;
}

std::string readAssetPath(const nlohmann::json& object) {
    std::string path = readString(object, "relativePath", "relative_path");
    if (path.empty()) path = readString(object, "sourcePath", "source_path");
    return path;
}

bool isPortableAssetPath(const std::string& sourcePath) {
    return isSafeProjectRelativePath(std::filesystem::u8path(sourcePath));
}

Vec4 readVec4(const nlohmann::json& value, Vec4 fallback = {}) {
    requireObject(value, "Vec4");
    return Vec4{
        readFloat(value, "r", fallback.r),
        readFloat(value, "g", fallback.g),
        readFloat(value, "b", fallback.b),
        readFloat(value, "a", fallback.a),
    };
}

Vec3 readVec3(const nlohmann::json& value, Vec3 fallback = {}) {
    requireObject(value, "Vec3");
    return Vec3{
        readFloat(value, "x", fallback.x),
        readFloat(value, "y", fallback.y),
        readFloat(value, "z", fallback.z),
    };
}

Transform readTransform(const nlohmann::json& value) {
    Transform transform;
    requireObject(value, "transform");
    if (value.contains("position")) transform.position = readVec2(value["position"]);
    if (value.contains("scale")) transform.scale = readVec2(value["scale"], {1.f, 1.f});
    transform.rotation = readFloat(value, "rotation", 0.f);
    return transform;
}

bool readInstance(const nlohmann::json& value, SceneInstanceDef& out) {
    requireObject(value, "scenes[].instances[]");
    out = SceneInstanceDef{};
    out.id = readNumber<EntityId>(value, "id", 0u, "scenes[].instances[].id");
    out.objectTypeId = readString(value, "objectTypeId", "object_type_id");
    out.instanceName = readString(value, "instanceName", "instance_name");
    if (value.contains("transform")) out.transform = readTransform(value["transform"]);
    out.visible = readBool(value, "visible", out.visible, "scenes[].instances[].visible");
    out.layerId = readString(value, "layerId", "layer_id");
    AssetId foldedRendererAnimationAssetId;
    if (const nlohmann::json* srValue = optionalObject(
            value, "spriteRenderer", "scenes[].instances[].spriteRenderer")) {
        const auto& sr = *srValue;
        SpriteRendererComponent component;
        component.imageAssetId = readString(sr, "imageAssetId", "image_asset_id");
        component.visible = readBool(
            sr, "visible", true, "scenes[].instances[].spriteRenderer.visible");
        foldedRendererAnimationAssetId = readString(
            sr, "animationAssetId", "animation_asset_id");
        out.legacySpriteRendererV3 = component;
    }
    if (const nlohmann::json* saValue = optionalObject(
            value, "spriteAnimator", "scenes[].instances[].spriteAnimator")) {
        const auto& sa = *saValue;
        SpriteAnimatorComponent component;
        component.animationAssetId = readString(
            sa, "animationAssetId", "animation_asset_id");
        component.defaultClipId = readDefaultClipId(sa);
        component.autoPlay = readBool(
            sa, "autoPlay", true, "scenes[].instances[].spriteAnimator.autoPlay");
        component.playbackSpeed = readFloat(sa, "playbackSpeed", 1.f);
        out.legacySpriteAnimatorV3 = component;
    }
    if (!foldedRendererAnimationAssetId.empty()) {
        if (!out.legacySpriteAnimatorV3) {
            out.legacySpriteAnimatorV3 = SpriteAnimatorComponent{};
        }
        if (out.legacySpriteAnimatorV3->animationAssetId.empty()) {
            out.legacySpriteAnimatorV3->animationAssetId =
                foldedRendererAnimationAssetId;
        }
    }
    AssetId foldedOverrideAnimationAssetId;
    if (const nlohmann::json* srValue = optionalObject(
            value, "spriteRendererOverride",
            "scenes[].instances[].spriteRendererOverride")) {
        const auto& sr = *srValue;
        SpriteRendererOverride delta;
        if (findMember(sr, "imageAssetId", "image_asset_id")) {
            delta.imageAssetId = readString(sr, "imageAssetId", "image_asset_id");
        }
        if (findMember(sr, "animationAssetId", "animation_asset_id")) {
            foldedOverrideAnimationAssetId = readString(
                sr, "animationAssetId", "animation_asset_id");
        }
        if (findMember(sr, "visible")) {
            delta.visible = readBool(
                sr, "visible", true,
                "scenes[].instances[].spriteRendererOverride.visible");
        }
        if (findMember(sr, "capabilityEnabled")) {
            delta.capabilityEnabled = readBool(
                sr, "capabilityEnabled", true,
                "scenes[].instances[].spriteRendererOverride.capabilityEnabled");
        }
        out.spriteRendererOverride = std::move(delta);
    }
    if (const nlohmann::json* saValue = optionalObject(
            value, "spriteAnimatorOverride",
            "scenes[].instances[].spriteAnimatorOverride")) {
        const auto& sa = *saValue;
        SpriteAnimatorOverride delta;
        if (findMember(sa, "animationAssetId", "animation_asset_id")) {
            delta.animationAssetId = readString(
                sa, "animationAssetId", "animation_asset_id");
        }
        if (findMember(sa, "defaultClipId", "default_clip_id")
            || findMember(sa, "initialClipId", "initial_clip_id")) {
            delta.defaultClipId = readDefaultClipId(sa);
        }
        if (findMember(sa, "autoPlay")) {
            delta.autoPlay = readBool(
                sa, "autoPlay", true,
                "scenes[].instances[].spriteAnimatorOverride.autoPlay");
        }
        if (findMember(sa, "playbackSpeed")) {
            delta.playbackSpeed = readFloat(sa, "playbackSpeed", 1.f);
        }
        if (findMember(sa, "capabilityEnabled")) {
            delta.capabilityEnabled = readBool(
                sa, "capabilityEnabled", true,
                "scenes[].instances[].spriteAnimatorOverride.capabilityEnabled");
        }
        out.spriteAnimatorOverride = std::move(delta);
    }
    if (!foldedOverrideAnimationAssetId.empty()) {
        if (!out.spriteAnimatorOverride) {
            out.spriteAnimatorOverride = SpriteAnimatorOverride{};
        }
        if (!out.spriteAnimatorOverride->animationAssetId) {
            out.spriteAnimatorOverride->animationAssetId =
                foldedOverrideAnimationAssetId;
        }
    }
    if (const nlohmann::json* tmValue = optionalObject(
            value, "tilemap", "scenes[].instances[].tilemap")) {
        const auto& tm = *tmValue;
        TilemapComponent component;
        component.tilesetAssetId = readString(tm, "tilesetAssetId", "tileset_asset_id");
        if (tm.contains("cellSize")) component.cellSize = readVec2(tm["cellSize"], component.cellSize);
        component.chunkSize = readNumber<int>(
            tm, "chunkSize", component.chunkSize, "scenes[].instances[].tilemap.chunkSize");
        if (const nlohmann::json* chunks = optionalArray(
                tm, "chunks", "scenes[].instances[].tilemap.chunks")) {
            for (const auto& chunkJson : *chunks) {
                requireObject(chunkJson, "scenes[].instances[].tilemap.chunks[]");
                TilemapChunk chunk;
                chunk.chunkX = readNumber<int>(
                    chunkJson, "chunkX", 0, "scenes[].instances[].tilemap.chunks[].chunkX");
                chunk.chunkY = readNumber<int>(
                    chunkJson, "chunkY", 0, "scenes[].instances[].tilemap.chunks[].chunkY");
                if (const nlohmann::json* cells = optionalArray(
                        chunkJson, "cells", "scenes[].instances[].tilemap.chunks[].cells")) {
                    for (const auto& cellJson : *cells) {
                        if (cellJson.is_object()) {
                            TilemapCellValue cell;
                            cell.tileId = readString(cellJson, "tileId", "tile_id");
                            cell.flags = static_cast<TileTransformFlags>(
                                readNumber<int>(cellJson, "flags", 0,
                                    "scenes[].instances[].tilemap.chunks[].cells[].flags"));
                            chunk.cells.push_back(std::move(cell));
                        } else if (cellJson.is_null()) {
                            chunk.cells.push_back(std::nullopt);   // null / missing = empty
                        } else {
                            invalidField("scenes[].instances[].tilemap.chunks[].cells[]",
                                         "object or null");
                        }
                    }
                }
                component.chunks.push_back(std::move(chunk));
            }
        }
        out.tilemap = std::move(component);
    }
    return true;
}

SceneDef readScene(const nlohmann::json& value, const SceneId& fallbackId) {
    SceneDef scene;
    requireObject(value, "scenes[]");
    scene.id = readString(value, "id", nullptr, fallbackId);
    scene.name = readString(value, "name", nullptr, scene.id);
    if (value.contains("worldSize")) {
        scene.worldSize = readVec2(value["worldSize"], scene.worldSize);
    }
    if (value.contains("viewportSize")) {
        scene.viewportSize = readVec2(value["viewportSize"], scene.viewportSize);
    }
    if (value.contains("backgroundColor")) {
        scene.backgroundColor = readVec4(value["backgroundColor"], scene.backgroundColor);
    }
    if (const nlohmann::json* instances = optionalArray(value, "instances", "scenes[].instances")) {
        for (const auto& item : *instances) {
            SceneInstanceDef instance;
            readInstance(item, instance);
            scene.instances.push_back(std::move(instance));
        }
    }
    if (const nlohmann::json* layers = optionalArray(value, "layers", "scenes[].layers")) {
        for (const auto& item : *layers) {
            requireObject(item, "scenes[].layers[]");
            SceneLayerDef layer;
            layer.id = readString(item, "id", nullptr);
            layer.name = readString(item, "name", nullptr, layer.id);
            layer.locked = readBool(item, "locked", layer.locked, "scenes[].layers[].locked");
            scene.layers.push_back(std::move(layer));
        }
    }
    scene.defaultLayerId = readString(value, "defaultLayerId", "default_layer_id");

    // Legacy canonical default layer is now the first authored layer. Only the
    // untouched old "Default" layer migrates; user-renamed default layers keep
    // their authored name and id.
    bool hasLayerOne = false;
    for (const SceneLayerDef& layer : scene.layers) {
        if (layer.id == "layer-1") {
            hasLayerOne = true;
            break;
        }
    }
    if (!hasLayerOne) {
        for (SceneLayerDef& layer : scene.layers) {
            if (layer.id == "default" && layer.name == "Default") {
                layer.id = "layer-1";
                layer.name = "Layer 1";
                if (scene.defaultLayerId.empty() || scene.defaultLayerId == "default") {
                    scene.defaultLayerId = "layer-1";
                }
                for (SceneInstanceDef& inst : scene.instances) {
                    if (inst.layerId == "default") inst.layerId = "layer-1";
                }
                break;
            }
        }
    }

    // Migration: every scene must have a real first layer; normalize the
    // default id and every instance to a real layer (legacy "" / dangling ->
    // default). No fictitious fallback survives past load.
    const auto layerExists = [&](const std::string& id) {
        for (const SceneLayerDef& l : scene.layers) if (l.id == id) return true;
        return false;
    };
    if (scene.layers.empty()) {
        scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1", false});
        scene.defaultLayerId = "layer-1";
    }
    if (!layerExists(scene.defaultLayerId)) scene.defaultLayerId = scene.layers.front().id;
    for (SceneInstanceDef& inst : scene.instances) {
        if (!layerExists(inst.layerId)) inst.layerId = scene.defaultLayerId;
    }
    return scene;
}

SceneId duplicateKeyFor(const SceneId& id, std::size_t index) {
    return id + "#duplicate-" + std::to_string(index);
}

void insertReadScene(ProjectDoc& out, SceneDef scene) {
    SceneId key = scene.id;
    if (out.scenes.find(key) != out.scenes.end()) {
        key = duplicateKeyFor(scene.id, out.scenes.size());
    }
    out.scenes.emplace(std::move(key), std::move(scene));
}

void readScenes(const nlohmann::json& root, ProjectDoc& out) {
    const auto it = root.find("scenes");
    if (it == root.end()) return;
    const auto& scenes = *it;
    if (scenes.is_array()) {
        for (const auto& item : scenes) {
            SceneDef scene = readScene(item, "scene_" + std::to_string(out.scenes.size()));
            insertReadScene(out, std::move(scene));
        }
    } else if (scenes.is_object()) {
        for (const auto& [key, value] : scenes.items()) {
            SceneDef scene = readScene(value, key);
            insertReadScene(out, std::move(scene));
        }
    } else {
        invalidField("scenes", "array or object");
    }
}

nlohmann::json vec4ToJson(const Vec4& v) {
    return nlohmann::json{{"r", v.r}, {"g", v.g}, {"b", v.b}, {"a", v.a}};
}

nlohmann::json transformToJson(const Transform& t) {
    return nlohmann::json{
        {"position", vec2ToJson(t.position)},
        {"scale", vec2ToJson(t.scale)},
        {"rotation", t.rotation},
    };
}

nlohmann::json instanceToJson(const SceneInstanceDef& instance) {
    nlohmann::json json{
        {"id", instance.id},
        {"objectTypeId", instance.objectTypeId},
        {"instanceName", instance.instanceName},
        {"transform", transformToJson(instance.transform)},
        {"visible", instance.visible},
        {"layerId", instance.layerId},
    };
    if (instance.spriteRendererOverride.has_value()) {
        const SpriteRendererOverride& deltaValue = *instance.spriteRendererOverride;
        nlohmann::json delta = nlohmann::json::object();
        if (deltaValue.imageAssetId) delta["imageAssetId"] = *deltaValue.imageAssetId;
        if (deltaValue.visible) delta["visible"] = *deltaValue.visible;
        if (deltaValue.capabilityEnabled) {
            delta["capabilityEnabled"] = *deltaValue.capabilityEnabled;
        }
        if (!delta.empty()) json["spriteRendererOverride"] = std::move(delta);
    }
    if (instance.spriteAnimatorOverride.has_value()) {
        const SpriteAnimatorOverride& deltaValue = *instance.spriteAnimatorOverride;
        nlohmann::json delta = nlohmann::json::object();
        if (deltaValue.animationAssetId) {
            delta["animationAssetId"] = *deltaValue.animationAssetId;
        }
        if (deltaValue.defaultClipId) delta["defaultClipId"] = *deltaValue.defaultClipId;
        if (deltaValue.autoPlay) delta["autoPlay"] = *deltaValue.autoPlay;
        if (deltaValue.playbackSpeed) delta["playbackSpeed"] = *deltaValue.playbackSpeed;
        if (deltaValue.capabilityEnabled) {
            delta["capabilityEnabled"] = *deltaValue.capabilityEnabled;
        }
        if (!delta.empty()) json["spriteAnimatorOverride"] = std::move(delta);
    }
    if (instance.tilemap.has_value()) {
        nlohmann::json chunks = nlohmann::json::array();
        for (const TilemapChunk& chunk : instance.tilemap->chunks) {
            nlohmann::json cells = nlohmann::json::array();
            for (const TilemapCell& cell : chunk.cells) {
                if (cell.has_value()) {
                    cells.push_back(nlohmann::json{
                        {"tileId", cell->tileId},
                        {"flags", static_cast<int>(cell->flags)},
                    });
                } else {
                    cells.push_back(nullptr);
                }
            }
            chunks.push_back(nlohmann::json{
                {"chunkX", chunk.chunkX},
                {"chunkY", chunk.chunkY},
                {"cells", std::move(cells)},
            });
        }
        json["tilemap"] = nlohmann::json{
            {"tilesetAssetId", instance.tilemap->tilesetAssetId},
            {"cellSize", vec2ToJson(instance.tilemap->cellSize)},
            {"chunkSize", instance.tilemap->chunkSize},
            {"chunks", std::move(chunks)},
        };
    }
    return json;
}

nlohmann::json animationFrameToJson(const SpriteFrameDef& frame) {
    return nlohmann::json{
        {"id", frame.id},
        {"x", frame.x},
        {"y", frame.y},
        {"width", frame.width},
        {"height", frame.height},
    };
}

nlohmann::json animationClipToJson(const SpriteAnimationClipDef& clip) {
    return nlohmann::json{
        {"id", clip.id},
        {"name", clip.name},
        {"frameIds", clip.frameIds},
        {"framesPerSecond", clip.framesPerSecond},
        {"playbackMode", playbackModeToString(clip.playbackMode)},
    };
}

// Minimal object-type persistence: only fields resolved or rendered by the
// native editor. v4 makes sprite presentation defaults explicit here.
nlohmann::json objectTypeToJson(const std::string& id, const EntityDef& def) {
    nlohmann::json json{
        {"id", id},
        {"name", def.name},
        {"visible", def.visible},
    };
    if (def.spriteRenderer.has_value()) {
        json["spriteRenderer"] = nlohmann::json{
            {"imageAssetId", def.spriteRenderer->imageAssetId},
            {"visible", def.spriteRenderer->visible},
        };
    }
    if (def.spriteAnimator.has_value()) {
        json["spriteAnimator"] = nlohmann::json{
            {"animationAssetId", def.spriteAnimator->animationAssetId},
            {"defaultClipId", def.spriteAnimator->defaultClipId},
            {"autoPlay", def.spriteAnimator->autoPlay},
            {"playbackSpeed", def.spriteAnimator->playbackSpeed},
        };
    }
    if (def.boxCollider2D.has_value()) {
        json["boxCollider2D"] = nlohmann::json{
            {"offset", vec2ToJson(def.boxCollider2D->offset)},
            {"size", vec2ToJson(def.boxCollider2D->size)},
            {"enabled", def.boxCollider2D->enabled},
            {"mode", BoxColliderModeToString(def.boxCollider2D->mode)},
        };
    }
    if (def.linearMover.has_value()) {
        // _paused is a runtime flag, deliberately not persisted.
        json["linearMover"] = nlohmann::json{
            {"directionX", def.linearMover->directionX},
            {"directionY", def.linearMover->directionY},
            {"speed", def.linearMover->speed},
        };
    }
    if (def.topDownController.has_value()) {
        json["topDownController"] = nlohmann::json{
            {"maxSpeed", def.topDownController->maxSpeed},
            {"acceleration", def.topDownController->acceleration},
            {"friction", def.topDownController->friction},
            {"fourDirections", def.topDownController->fourDirections},
        };
    }
    if (def.platformerController.has_value()) {
        // Native editor persists the authored subset: Move Speed / Jump Speed /
        // Gravity (the other canonical fields stay at their defaults on load).
        json["platformerController"] = nlohmann::json{
            {"moveSpeed", def.platformerController->maxSpeed},
            {"jumpSpeed", def.platformerController->jumpForce},
            {"gravity", def.platformerController->customGravity},
        };
    }
    if (def.scripts.has_value()) {
        nlohmann::json attachments = nlohmann::json::array();
        for (const ScriptAttachmentDef& attachment : def.scripts->attachments) {
            attachments.push_back(nlohmann::json{
                {"id", attachment.id},
                {"scriptAssetId", attachment.scriptAssetId},
                {"enabled", attachment.enabled},
            });
        }
        json["scripts"] = nlohmann::json{
            {"attachments", std::move(attachments)},
        };
    }
    if (def.logicBoard.has_value()) {
        json["logicBoard"] = Logic::logicBoardToJson(*def.logicBoard);
    }
    return json;
}

nlohmann::json sceneToJson(const SceneDef& scene) {
    nlohmann::json instances = nlohmann::json::array();
    for (const SceneInstanceDef& instance : scene.instances) {
        instances.push_back(instanceToJson(instance));
    }
    // Per-scene render layers: the order of the array IS the render order.
    nlohmann::json layers = nlohmann::json::array();
    for (const SceneLayerDef& layer : scene.layers) {
        layers.push_back(nlohmann::json{
            {"id", layer.id}, {"name", layer.name}, {"locked", layer.locked}});
    }

    return nlohmann::json{
        {"id", scene.id},
        {"name", scene.name},
        {"worldSize", vec2ToJson(scene.worldSize)},
        {"viewportSize", vec2ToJson(scene.viewportSize)},
        {"backgroundColor", vec4ToJson(scene.backgroundColor)},
        {"layers", std::move(layers)},
        {"defaultLayerId", scene.defaultLayerId},
        {"instances", std::move(instances)},
    };
}

bool empty(const SpriteRendererOverride& delta) {
    return !delta.imageAssetId && !delta.visible && !delta.capabilityEnabled;
}

bool empty(const SpriteAnimatorOverride& delta) {
    return !delta.animationAssetId && !delta.defaultClipId && !delta.autoPlay
        && !delta.playbackSpeed && !delta.capabilityEnabled;
}

SpriteRendererOverride rendererDelta(const SpriteRendererComponent& value,
                                     const SpriteRendererComponent& defaults) {
    SpriteRendererOverride delta;
    if (value.imageAssetId != defaults.imageAssetId) delta.imageAssetId = value.imageAssetId;
    if (value.visible != defaults.visible) delta.visible = value.visible;
    return delta;
}

SpriteAnimatorOverride animatorDelta(const SpriteAnimatorComponent& value,
                                     const SpriteAnimatorComponent& defaults) {
    SpriteAnimatorOverride delta;
    if (value.animationAssetId != defaults.animationAssetId) {
        delta.animationAssetId = value.animationAssetId;
    }
    if (value.defaultClipId != defaults.defaultClipId) {
        delta.defaultClipId = value.defaultClipId;
    }
    if (value.autoPlay != defaults.autoPlay) delta.autoPlay = value.autoPlay;
    if (value.playbackSpeed != defaults.playbackSpeed) {
        delta.playbackSpeed = value.playbackSpeed;
    }
    return delta;
}

std::vector<SceneInstanceDef*> instancesInCanonicalOrder(ProjectDoc& document,
                                                          const std::string& objectTypeId) {
    // ProjectDoc stores scenes in an unordered_map. Canonical lexicographic
    // scene-id order makes the v3 promotion stable across machines and saves;
    // instance vector order remains the persisted authoring order.
    std::vector<std::pair<SceneId, SceneDef*>> scenes;
    scenes.reserve(document.scenes.size());
    for (auto& [sceneKey, scene] : document.scenes) {
        scenes.emplace_back(sceneKey, &scene);
    }
    std::sort(scenes.begin(), scenes.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    std::vector<SceneInstanceDef*> result;
    for (const auto& [_, scene] : scenes) {
        for (SceneInstanceDef& instance : scene->instances) {
            if (instance.objectTypeId == objectTypeId) result.push_back(&instance);
        }
    }
    return result;
}

void migrateSpriteOwnershipToObjectTypes(ProjectDoc& document) {
    // A legacy catalog-less document may still have authored per-instance
    // presentation. Create only the missing types needed to preserve that
    // capability; completely component-less free labels remain untouched.
    std::vector<std::string> missingTypes;
    for (const auto& [_, scene] : document.scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if ((instance.legacySpriteRendererV3 || instance.legacySpriteAnimatorV3)
                && document.objectTypes.find(instance.objectTypeId)
                       == document.objectTypes.end()) {
                missingTypes.push_back(instance.objectTypeId);
            }
        }
    }
    std::sort(missingTypes.begin(), missingTypes.end());
    missingTypes.erase(std::unique(missingTypes.begin(), missingTypes.end()),
                       missingTypes.end());
    for (const std::string& id : missingTypes) {
        EntityDef type;
        type.className = id;
        type.name = id;
        document.objectTypes.emplace(id, std::move(type));
    }

    for (auto& [objectTypeId, type] : document.objectTypes) {
        std::vector<SceneInstanceDef*> instances =
            instancesInCanonicalOrder(document, objectTypeId);

        bool rendererPromotedFromInstance = false;
        if (!type.spriteRenderer && !type.sprite.spriteAssetId.empty()) {
            type.spriteRenderer = SpriteRendererComponent{
                type.sprite.spriteAssetId,
                type.visible,
            };
        }
        if (!type.spriteRenderer) {
            const auto first = std::find_if(instances.begin(), instances.end(),
                [](const SceneInstanceDef* instance) {
                    return instance->legacySpriteRendererV3.has_value();
                });
            if (first != instances.end()) {
                type.spriteRenderer = *(*first)->legacySpriteRendererV3;
                rendererPromotedFromInstance = true;
            }
        }

        for (SceneInstanceDef* instance : instances) {
            if (instance->legacySpriteRendererV3 && type.spriteRenderer
                && !instance->spriteRendererOverride) {
                SpriteRendererOverride delta = rendererDelta(
                    *instance->legacySpriteRendererV3, *type.spriteRenderer);
                if (!empty(delta)) instance->spriteRendererOverride = std::move(delta);
            } else if (!instance->legacySpriteRendererV3 && type.spriteRenderer
                       && rendererPromotedFromInstance
                       && !instance->spriteRendererOverride) {
                SpriteRendererOverride disabled;
                disabled.capabilityEnabled = false;
                instance->spriteRendererOverride = std::move(disabled);
            }
            instance->legacySpriteRendererV3.reset();
        }

        bool animatorPromotedFromInstance = false;
        if (!type.spriteAnimator) {
            const auto first = std::find_if(instances.begin(), instances.end(),
                [](const SceneInstanceDef* instance) {
                    return instance->legacySpriteAnimatorV3.has_value();
                });
            if (first != instances.end()) {
                type.spriteAnimator = *(*first)->legacySpriteAnimatorV3;
                animatorPromotedFromInstance = true;
            }
        }

        for (SceneInstanceDef* instance : instances) {
            if (instance->legacySpriteAnimatorV3 && type.spriteAnimator
                && !instance->spriteAnimatorOverride) {
                SpriteAnimatorOverride delta = animatorDelta(
                    *instance->legacySpriteAnimatorV3, *type.spriteAnimator);
                if (!empty(delta)) instance->spriteAnimatorOverride = std::move(delta);
            } else if (!instance->legacySpriteAnimatorV3 && type.spriteAnimator
                       && animatorPromotedFromInstance
                       && !instance->spriteAnimatorOverride) {
                SpriteAnimatorOverride disabled;
                disabled.capabilityEnabled = false;
                instance->spriteAnimatorOverride = std::move(disabled);
            }
            instance->legacySpriteAnimatorV3.reset();
        }
    }
}

struct LegacyV8FrameRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const LegacyV8FrameRect& other) const {
        return x == other.x && y == other.y
            && width == other.width && height == other.height;
    }
};

struct LegacyV8Clip {
    AnimationClipId id;
    std::string name;
    AssetId imageId;
    std::vector<LegacyV8FrameRect> frames;
    float framesPerSecond = 12.f;
    AnimationPlaybackMode playbackMode = AnimationPlaybackMode::Loop;
};

SpriteAnimationAssetDef buildV9AssetFromV8Clips(
    AssetId assetId,
    std::string assetName,
    const AssetId& sourceImageAssetId,
    const std::vector<LegacyV8Clip>& clips) {
    SpriteAnimationAssetDef asset;
    asset.id = std::move(assetId);
    asset.name = std::move(assetName);
    asset.sourceImageAssetId = sourceImageAssetId;

    std::map<std::tuple<int, int, int, int>, SpriteFrameId> rectToId;
    for (const LegacyV8Clip& clip : clips) {
        SpriteAnimationClipDef outClip;
        outClip.id = clip.id;
        outClip.name = clip.name;
        outClip.framesPerSecond = clip.framesPerSecond;
        outClip.playbackMode = clip.playbackMode;
        for (const LegacyV8FrameRect& frame : clip.frames) {
            const auto key = std::make_tuple(frame.x, frame.y, frame.width, frame.height);
            auto it = rectToId.find(key);
            if (it == rectToId.end()) {
                SpriteFrameDef poolFrame;
                poolFrame.id = "frame-" + std::to_string(asset.frames.size() + 1);
                poolFrame.x = frame.x;
                poolFrame.y = frame.y;
                poolFrame.width = frame.width;
                poolFrame.height = frame.height;
                it = rectToId.emplace(key, poolFrame.id).first;
                asset.frames.push_back(std::move(poolFrame));
            }
            outClip.frameIds.push_back(it->second);
        }
        asset.clips.push_back(std::move(outClip));
    }
    return asset;
}

void appendSplitV8AnimationAssets(
    ProjectDoc& document,
    const AssetId& originalId,
    const std::string& originalName,
    const std::string& legacyDefaultClipId,
    std::vector<LegacyV8Clip> clips) {
    if (clips.empty()) {
        SpriteAnimationAssetDef empty;
        empty.id = originalId;
        empty.name = originalName;
        document.spriteAnimationAssets.push_back(std::move(empty));
        return;
    }

    AssetId keepIdImage;
    if (!legacyDefaultClipId.empty()) {
        for (const LegacyV8Clip& clip : clips) {
            if (clip.id == legacyDefaultClipId) {
                keepIdImage = clip.imageId;
                break;
            }
        }
    }
    if (keepIdImage.empty()) keepIdImage = clips.front().imageId;

    std::map<AssetId, std::vector<LegacyV8Clip>> byImage;
    for (LegacyV8Clip& clip : clips) {
        byImage[clip.imageId].push_back(std::move(clip));
    }

    for (auto& [imageId, group] : byImage) {
        AssetId assetId = (imageId == keepIdImage)
            ? originalId
            : originalId + "-img-" + sanitizeImageIdFragment(imageId);
        document.spriteAnimationAssets.push_back(buildV9AssetFromV8Clips(
            std::move(assetId), originalName, imageId, group));
    }
}

void migrateSpriteAnimationOwnershipV9(ProjectDoc& document) {
    // Ownership fold from SpriteRenderer.animationAssetId is applied during JSON
    // read (types no longer carry that field). Remap references when a v8
    // multi-image asset was split so clip ids still resolve.
    auto remapAnimator = [&](SpriteAnimatorComponent& animator) {
        animator.animationAssetId = resolveAnimationAssetForClip(
            document, animator.animationAssetId, animator.defaultClipId);
    };
    auto remapOverride = [&](SpriteAnimatorOverride& delta,
                             const SpriteAnimatorComponent* typeAnimator) {
        const AssetId animationAssetId = delta.animationAssetId
            ? *delta.animationAssetId
            : (typeAnimator ? typeAnimator->animationAssetId : AssetId{});
        const std::string clipId = delta.defaultClipId
            ? *delta.defaultClipId
            : (typeAnimator ? typeAnimator->defaultClipId : std::string{});
        const AssetId resolved =
            resolveAnimationAssetForClip(document, animationAssetId, clipId);
        if (resolved != animationAssetId) {
            delta.animationAssetId = resolved;
        }
    };
    auto remapLogicBoard = [&](LogicBoardDef& board) {
        for (LogicRuleDef& rule : board.rules) {
            for (LogicBlockDef& action : rule.actions) {
                if (action.typeId != Logic::kAnimationPlayClip) continue;
                LogicPropertyDef* assetProp = nullptr;
                LogicPropertyDef* clipProp = nullptr;
                for (LogicPropertyDef& property : action.properties) {
                    if (property.key == "animationAssetId") assetProp = &property;
                    if (property.key == "clipId") clipProp = &property;
                }
                if (!assetProp || !clipProp) continue;
                const auto* assetRef =
                    std::get_if<LogicAssetReference>(&assetProp->value);
                const auto* clipRef =
                    std::get_if<LogicStringValue>(&clipProp->value);
                if (!assetRef || !clipRef) continue;
                const AssetId resolved = resolveAnimationAssetForClip(
                    document, assetRef->id, clipRef->value);
                if (resolved != assetRef->id) {
                    assetProp->value = LogicAssetReference{resolved};
                }
            }
        }
    };

    for (auto& [_, type] : document.objectTypes) {
        if (type.spriteAnimator) remapAnimator(*type.spriteAnimator);
        if (type.logicBoard) remapLogicBoard(*type.logicBoard);
    }
    for (auto& [_, scene] : document.scenes) {
        for (SceneInstanceDef& instance : scene.instances) {
            if (!instance.spriteAnimatorOverride) continue;
            const EntityDef* type = nullptr;
            const auto typeIt = document.objectTypes.find(instance.objectTypeId);
            if (typeIt != document.objectTypes.end()) type = &typeIt->second;
            remapOverride(
                *instance.spriteAnimatorOverride,
                type && type->spriteAnimator ? &*type->spriteAnimator : nullptr);
        }
    }
}

} // namespace

DeserializeResult ProjectSerializer::deserialize(std::string_view source) {
    try {
    const nlohmann::json root = nlohmann::json::parse(source.begin(), source.end());
    if (!root.is_object()) invalidField("$", "object");

    ProjectDoc doc;
    doc.projectName = readString(root, "projectName", "project_name", "Untitled");
    doc.version = readString(root, "version", nullptr, "2.0.0");
    doc.licenseTier = readString(root, "licenseTier", "license_tier", "free");
    doc.targetFPS = readFloat(root, "targetFPS", 60.f);
    doc.activeSceneId = readString(root, "activeSceneId", "active_scene_id");
    doc.mainScriptPath = readString(root, "mainScriptPath", "main_script_path",
                                    "scripts/main.luac");
    // schemaVersion is emitted for external tooling while formatVersion remains
    // the migration authority. When present it must still be a representable
    // integer; malformed metadata is never silently ignored.
    (void)readNumber<int>(root, "schemaVersion", 0, "schemaVersion");
    doc.formatVersion = readAliasedNumber<int>(
        root, "formatVersion", "format_version", 0, "formatVersion");
    readScenes(root, doc);

    if (const nlohmann::json* objectTypes = optionalArray(root, "objectTypes", "objectTypes")) {
        std::unordered_set<std::string> seenTypeIds;
        for (const auto& item : *objectTypes) {
            requireObject(item, "objectTypes[]");
            const std::string id = readString(item, "id", "className");
            if (!seenTypeIds.insert(id).second) {
                return DeserializeResult::failure("Duplicate object type id");
            }
            EntityDef def;
            def.className = id;
            def.name = readString(item, "name", nullptr, id);
            def.visible = readBool(item, "visible", true, "objectTypes[].visible");
            if (const nlohmann::json* spriteValue = optionalObject(
                    item, "sprite", "objectTypes[].sprite")) {
                const auto& sprite = *spriteValue;
                def.sprite.spriteAssetId = readString(sprite, "spriteAssetId", "sprite_asset_id");
                if (sprite.contains("fillColor")) {
                    def.sprite.fillColor = readVec3(sprite["fillColor"], def.sprite.fillColor);
                }
            }
            if (const nlohmann::json* srValue = optionalObject(
                    item, "spriteRenderer", "objectTypes[].spriteRenderer")) {
                const auto& sr = *srValue;
                SpriteRendererComponent component;
                component.imageAssetId = readString(sr, "imageAssetId", "image_asset_id");
                component.visible = readBool(
                    sr, "visible", component.visible,
                    "objectTypes[].spriteRenderer.visible");
                const AssetId foldedAnimationAssetId = readString(
                    sr, "animationAssetId", "animation_asset_id");
                def.spriteRenderer = std::move(component);
                if (!foldedAnimationAssetId.empty()) {
                    if (!def.spriteAnimator) {
                        def.spriteAnimator = SpriteAnimatorComponent{};
                    }
                    if (def.spriteAnimator->animationAssetId.empty()) {
                        def.spriteAnimator->animationAssetId = foldedAnimationAssetId;
                    }
                }
            }
            if (const nlohmann::json* saValue = optionalObject(
                    item, "spriteAnimator", "objectTypes[].spriteAnimator")) {
                const auto& sa = *saValue;
                SpriteAnimatorComponent component = def.spriteAnimator.value_or(
                    SpriteAnimatorComponent{});
                if (findMember(sa, "animationAssetId", "animation_asset_id")) {
                    component.animationAssetId = readString(
                        sa, "animationAssetId", "animation_asset_id");
                }
                if (findMember(sa, "defaultClipId", "default_clip_id")
                    || findMember(sa, "initialClipId", "initial_clip_id")) {
                    component.defaultClipId = readDefaultClipId(sa);
                }
                component.autoPlay = readBool(
                    sa, "autoPlay", component.autoPlay,
                    "objectTypes[].spriteAnimator.autoPlay");
                component.playbackSpeed = readFloat(
                    sa, "playbackSpeed", component.playbackSpeed);
                def.spriteAnimator = std::move(component);
            }
            if (const nlohmann::json* colliderValue = optionalObject(
                    item, "boxCollider2D", "objectTypes[].boxCollider2D")) {
                const auto& collider = *colliderValue;
                BoxCollider2DComponent component;
                if (collider.contains("offset")) {
                    component.offset = readVec2(collider["offset"], component.offset);
                }
                if (collider.contains("size")) {
                    component.size = readVec2(collider["size"], component.size);
                }
                component.enabled = readBool(
                    collider, "enabled", component.enabled,
                    "objectTypes[].boxCollider2D.enabled");
                if (collider.contains("mode")) {
                    if (!collider["mode"].is_string()) {
                        return DeserializeResult::failure("BoxCollider2D mode is invalid");
                    }
                    const auto parsed = BoxColliderModeFromString(collider["mode"].get<std::string>());
                    if (!parsed.has_value()) {
                        return DeserializeResult::failure("BoxCollider2D mode is unknown");
                    }
                    const bool legacy = readBool(
                        collider, "isTrigger", *parsed == BoxColliderMode::Trigger,
                        "objectTypes[].boxCollider2D.isTrigger");
                    if (collider.contains("isTrigger")
                        && legacyTriggerMode(legacy) != *parsed) {
                        return DeserializeResult::failure(
                            "BoxCollider2D mode conflicts with legacy isTrigger");
                    }
                    component.mode = *parsed;
                } else if (collider.contains("isTrigger")) {
                    component.mode = legacyTriggerMode(readBool(
                        collider, "isTrigger", false,
                        "objectTypes[].boxCollider2D.isTrigger"));
                }
                def.boxCollider2D = component;
            }
            if (const nlohmann::json* moverValue = optionalObject(
                    item, "linearMover", "objectTypes[].linearMover")) {
                const auto& m = *moverValue;
                LinearMoverComponent component;
                component.directionX = readFloat(m, "directionX", component.directionX);
                component.directionY = readFloat(m, "directionY", component.directionY);
                component.speed = readFloat(m, "speed", component.speed);
                def.linearMover = component;
            }
            if (const nlohmann::json* topDownValue = optionalObject(
                    item, "topDownController", "objectTypes[].topDownController")) {
                const auto& t = *topDownValue;
                TopDownControllerComponent component;
                component.maxSpeed = readFloat(t, "maxSpeed", component.maxSpeed);
                component.acceleration = readFloat(t, "acceleration", component.acceleration);
                component.friction = readFloat(t, "friction", component.friction);
                component.fourDirections = readBool(
                    t, "fourDirections", component.fourDirections,
                    "objectTypes[].topDownController.fourDirections");
                def.topDownController = component;
            }
            if (const nlohmann::json* platformerValue = optionalObject(
                    item, "platformerController", "objectTypes[].platformerController")) {
                const auto& p = *platformerValue;
                PlatformerControllerComponent component;   // others keep defaults
                component.maxSpeed = readFloat(p, "moveSpeed", component.maxSpeed);
                component.jumpForce = readFloat(p, "jumpSpeed", component.jumpForce);
                component.customGravity = readFloat(p, "gravity", component.customGravity);
                def.platformerController = component;
            }
            if (const nlohmann::json* scriptsValue = optionalObject(
                    item, "scripts", "objectTypes[].scripts")) {
                ScriptComponent component;
                if (const nlohmann::json* attachments = optionalArray(
                        *scriptsValue, "attachments", "objectTypes[].scripts.attachments")) {
                    for (const auto& attachmentValue : *attachments) {
                        requireObject(
                            attachmentValue, "objectTypes[].scripts.attachments[]");
                        ScriptAttachmentDef attachment;
                        attachment.id = readString(attachmentValue, "id", nullptr);
                        attachment.scriptAssetId = readString(
                            attachmentValue, "scriptAssetId", "script_asset_id");
                        attachment.enabled = readBool(
                            attachmentValue, "enabled", true,
                            "objectTypes[].scripts.attachments[].enabled");
                        component.attachments.push_back(std::move(attachment));
                    }
                }
                def.scripts = std::move(component);
            }
            if (item.contains("logicBoard")) {
                LogicBoardDef board;
                const Logic::LogicJsonResult parsed =
                    Logic::logicBoardFromJson(item["logicBoard"], board);
                if (!parsed.ok) return DeserializeResult::failure(parsed.error);
                // Object Types are decoded in file order; cross-type references
                // are checked only after the complete catalog exists below.
                const auto diagnostics = Logic::validateBoard(
                    id, board, &def, nullptr, Logic::ValidationMode::Authoring);
                const auto error = std::find_if(
                    diagnostics.begin(), diagnostics.end(),
                    [](const Logic::LogicDiagnostic& diagnostic) {
                        return diagnostic.severity == Logic::DiagnosticSeverity::Error;
                    });
                if (error != diagnostics.end()) {
                    return DeserializeResult::failure(
                        error->code + ": " + error->message);
                }
                def.logicBoard = std::move(board);
            }
            doc.objectTypes.emplace(id, std::move(def));
        }
    }

    if (const nlohmann::json* imageAssets = optionalArray(root, "imageAssets", "imageAssets")) {
        for (const auto& item : *imageAssets) {
            requireObject(item, "imageAssets[]");
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            ImageAssetDef asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.sourcePath = readAssetPath(item);
            doc.imageAssets.push_back(std::move(asset));
        }
    }

    if (const nlohmann::json* tilesets = optionalArray(root, "tilesets", "tilesets")) {
        for (const auto& item : *tilesets) {
            requireObject(item, "tilesets[]");
            const std::string assetId = readString(item, "assetId", "asset_id");
            TilesetAsset asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.imageAssetId = readString(item, "imageAssetId", "image_asset_id");
            if (const nlohmann::json* slicingValue = optionalObject(
                    item, "slicing", "tilesets[].slicing")) {
                const auto& s = *slicingValue;
                asset.slicing.tileWidth = readNumber<int>(s, "tileWidth", 32,
                                                           "tilesets[].slicing.tileWidth");
                asset.slicing.tileHeight = readNumber<int>(s, "tileHeight", 32,
                                                            "tilesets[].slicing.tileHeight");
                asset.slicing.marginX = readNumber<int>(s, "marginX", 0,
                                                         "tilesets[].slicing.marginX");
                asset.slicing.marginY = readNumber<int>(s, "marginY", 0,
                                                         "tilesets[].slicing.marginY");
                asset.slicing.spacingX = readNumber<int>(s, "spacingX", 0,
                                                          "tilesets[].slicing.spacingX");
                asset.slicing.spacingY = readNumber<int>(s, "spacingY", 0,
                                                          "tilesets[].slicing.spacingY");
            }
            if (const nlohmann::json* tiles = optionalArray(item, "tiles", "tilesets[].tiles")) {
                for (const auto& t : *tiles) {
                    requireObject(t, "tilesets[].tiles[]");
                    TileDefinition tile;
                    tile.id     = readString(t, "id", nullptr);
                    tile.x = readNumber<int>(t, "x", 0, "tilesets[].tiles[].x");
                    tile.y = readNumber<int>(t, "y", 0, "tilesets[].tiles[].y");
                    tile.width = readNumber<int>(t, "width", 0, "tilesets[].tiles[].width");
                    tile.height = readNumber<int>(t, "height", 0, "tilesets[].tiles[].height");
                    asset.tiles.push_back(std::move(tile));
                }
            }
            doc.tilesets.push_back(std::move(asset));
        }
    }

    if (const nlohmann::json* animationAssets = optionalArray(
            root, "spriteAnimationAssets", "spriteAnimationAssets")) {
        for (const auto& item : *animationAssets) {
            requireObject(item, "spriteAnimationAssets[]");
            const AssetId assetId = readString(item, "id", "asset_id");
            const std::string assetName = readString(item, "name", nullptr, assetId);
            const nlohmann::json* clipsJson = optionalArray(
                item, "clips", "spriteAnimationAssets[].clips");

            bool hasV9Frames = item.contains("frames") && item["frames"].is_array();
            bool hasV9Source = findMember(
                item, "sourceImageAssetId", "source_image_asset_id") != nullptr;
            bool hasV9FrameIds = false;
            bool hasV8EmbeddedFrames = false;
            bool hasLegacyClipImage = false;
            if (clipsJson) {
                for (const auto& clipJson : *clipsJson) {
                    if (!clipJson.is_object()) continue;
                    if (clipJson.contains("frameIds") && clipJson["frameIds"].is_array()) {
                        hasV9FrameIds = true;
                    }
                    if (clipJson.contains("frames") && clipJson["frames"].is_array()) {
                        hasV8EmbeddedFrames = true;
                    }
                    if (findMember(clipJson, "imageId", "image_id")) {
                        hasLegacyClipImage = true;
                    }
                }
            }
            const bool hasLegacyAssetImage =
                findMember(item, "imageId", "image_id") != nullptr;
            const bool parseAsV9 = hasV9Frames || hasV9Source || hasV9FrameIds
                || !(hasV8EmbeddedFrames || hasLegacyAssetImage || hasLegacyClipImage);

            if (parseAsV9) {
                SpriteAnimationAssetDef asset;
                asset.id = assetId;
                asset.name = assetName;
                asset.sourceImageAssetId = readString(
                    item, "sourceImageAssetId", "source_image_asset_id");
                if (const nlohmann::json* frames = optionalArray(
                        item, "frames", "spriteAnimationAssets[].frames")) {
                    for (const auto& frameJson : *frames) {
                        requireObject(frameJson, "spriteAnimationAssets[].frames[]");
                        SpriteFrameDef frame;
                        frame.id = readString(frameJson, "id", nullptr);
                        frame.x = readNumber<int>(frameJson, "x", 0,
                            "spriteAnimationAssets[].frames[].x");
                        frame.y = readNumber<int>(frameJson, "y", 0,
                            "spriteAnimationAssets[].frames[].y");
                        frame.width = readNumber<int>(frameJson, "width", 0,
                            "spriteAnimationAssets[].frames[].width");
                        frame.height = readNumber<int>(frameJson, "height", 0,
                            "spriteAnimationAssets[].frames[].height");
                        asset.frames.push_back(std::move(frame));
                    }
                }
                if (clipsJson) {
                    for (const auto& clipJson : *clipsJson) {
                        requireObject(clipJson, "spriteAnimationAssets[].clips[]");
                        SpriteAnimationClipDef clip;
                        clip.id = readString(clipJson, "id", nullptr);
                        clip.name = readString(clipJson, "name", nullptr, clip.id);
                        clip.framesPerSecond = readFloat(
                            clipJson, "framesPerSecond", clip.framesPerSecond);
                        const std::string playback =
                            readString(clipJson, "playbackMode", nullptr, "loop");
                        const auto mode = playbackModeFromString(playback);
                        if (!mode.has_value()) {
                            return DeserializeResult::failure(
                                "Animation clip playbackMode is unknown");
                        }
                        clip.playbackMode = *mode;
                        if (const nlohmann::json* frameIds = optionalArray(
                                clipJson, "frameIds",
                                "spriteAnimationAssets[].clips[].frameIds")) {
                            for (const auto& frameIdJson : *frameIds) {
                                if (!frameIdJson.is_string()) {
                                    invalidField(
                                        "spriteAnimationAssets[].clips[].frameIds[]",
                                        "string");
                                }
                                clip.frameIds.push_back(frameIdJson.get<std::string>());
                            }
                        }
                        asset.clips.push_back(std::move(clip));
                    }
                }
                doc.spriteAnimationAssets.push_back(std::move(asset));
                continue;
            }

            // v8: per-clip imageId + embedded frames; may split by image.
            const std::string legacyAssetImage = readString(item, "imageId", "image_id");
            const std::string legacyDefaultClipId = readString(
                item, "defaultClipId", "default_clip_id");
            std::vector<LegacyV8Clip> legacyClips;
            if (clipsJson) {
                for (const auto& clipJson : *clipsJson) {
                    requireObject(clipJson, "spriteAnimationAssets[].clips[]");
                    LegacyV8Clip clip;
                    clip.id = readString(clipJson, "id", nullptr);
                    clip.name = readString(clipJson, "name", nullptr, clip.id);
                    clip.imageId = readString(clipJson, "imageId", "image_id");
                    if (clip.imageId.empty()) clip.imageId = legacyAssetImage;
                    clip.framesPerSecond = readFloat(
                        clipJson, "framesPerSecond", clip.framesPerSecond);
                    const std::string playback =
                        readString(clipJson, "playbackMode", nullptr, "loop");
                    const auto mode = playbackModeFromString(playback);
                    if (!mode.has_value()) {
                        return DeserializeResult::failure(
                            "Animation clip playbackMode is unknown");
                    }
                    clip.playbackMode = *mode;
                    if (const nlohmann::json* frames = optionalArray(
                            clipJson, "frames",
                            "spriteAnimationAssets[].clips[].frames")) {
                        for (const auto& frameJson : *frames) {
                            requireObject(
                                frameJson, "spriteAnimationAssets[].clips[].frames[]");
                            LegacyV8FrameRect frame;
                            frame.x = readNumber<int>(frameJson, "x", 0,
                                "spriteAnimationAssets[].clips[].frames[].x");
                            frame.y = readNumber<int>(frameJson, "y", 0,
                                "spriteAnimationAssets[].clips[].frames[].y");
                            frame.width = readNumber<int>(frameJson, "width", 0,
                                "spriteAnimationAssets[].clips[].frames[].width");
                            frame.height = readNumber<int>(frameJson, "height", 0,
                                "spriteAnimationAssets[].clips[].frames[].height");
                            clip.frames.push_back(frame);
                        }
                    }
                    legacyClips.push_back(std::move(clip));
                }
            }
            appendSplitV8AnimationAssets(
                doc, assetId, assetName, legacyDefaultClipId, std::move(legacyClips));
        }
    }

    if (const nlohmann::json* audioAssets = optionalArray(root, "audioAssets", "audioAssets")) {
        for (const auto& item : *audioAssets) {
            requireObject(item, "audioAssets[]");
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            AudioAssetDef asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.sourcePath = readAssetPath(item);
            const std::string loadMode = readString(item, "loadMode", nullptr, "static");
            const auto parsed = audioLoadModeFromString(loadMode);
            if (!parsed.has_value()) {
                return DeserializeResult::failure("Audio asset loadMode is unknown");
            }
            asset.loadMode = *parsed;
            if (item.contains("generatedFromSfxId") && item["generatedFromSfxId"].is_string()) {
                const std::string from = item["generatedFromSfxId"].get<std::string>();
                if (!from.empty()) asset.generatedFromSfxId = from;
            }
            doc.audioAssets.push_back(std::move(asset));
        }
    }

    if (const nlohmann::json* generatedSfx = optionalArray(
            root, "generatedSfx", "generatedSfx")) {
        for (const auto& item : *generatedSfx) {
            requireObject(item, "generatedSfx[]");
            auto decoded = artcade::sfx::deserializeRecipeJson(item.dump());
            if (!decoded.ok()) {
                return DeserializeResult::failure(
                    "Generated SFX recipe is invalid: " + decoded.error().message);
            }
            doc.generatedSfx.push_back(decoded.takeValue());
        }
    }

    if (const nlohmann::json* fontAssets = optionalArray(root, "fontAssets", "fontAssets")) {
        for (const auto& item : *fontAssets) {
            requireObject(item, "fontAssets[]");
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            FontAssetDef asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.sourcePath = readAssetPath(item);
            asset.defaultPixelSize = readNumber<int>(
                item, "defaultPixelSize", 32, "fontAssets[].defaultPixelSize");
            const std::string preset = readString(item, "glyphPreset", nullptr, "european");
            const auto parsed = fontGlyphPresetFromString(preset);
            if (!parsed.has_value()) {
                return DeserializeResult::failure("Font asset glyphPreset is unknown");
            }
            asset.glyphPreset = *parsed;
            doc.fontAssets.push_back(std::move(asset));
        }
    }

    if (const nlohmann::json* scriptAssets = optionalArray(
            root, "scriptAssets", "scriptAssets")) {
        for (const auto& item : *scriptAssets) {
            requireObject(item, "scriptAssets[]");
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            ScriptAssetDef asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.sourcePath = readAssetPath(item);
            doc.scriptAssets.push_back(std::move(asset));
        }
    }

    return DeserializeResult::success(ProjectDocument{std::move(doc)});
    } catch (const JsonReadError& error) {
        return DeserializeResult::failure(error.what());
    } catch (const nlohmann::json::exception& error) {
        return DeserializeResult::failure(
            std::string("Project JSON is malformed: ") + error.what());
    } catch (const std::exception& error) {
        return DeserializeResult::failure(
            std::string("Project JSON could not be read: ") + error.what());
    } catch (...) {
        return DeserializeResult::failure("Project JSON could not be read: unknown error");
    }
}

SerializeResult ProjectSerializer::serialize(const ProjectDocument& document) {
    // Keep the current writer strict even while older in-memory callers still hand
    // us v3 instance components during the staged rollout. The normalization
    // is pure: saving never mutates the live ProjectDocument.
    ProjectDoc normalized = document.data();
    migrateSpriteOwnershipToObjectTypes(normalized);
    normalized.formatVersion = kCurrentSchemaVersion;
    const ProjectDoc& doc = normalized;
    nlohmann::json scenes = nlohmann::json::object();
    for (const auto& [sceneId, scene] : doc.scenes) {
        scenes[sceneId] = sceneToJson(scene);
    }

    nlohmann::json globalVariables = nlohmann::json::array();
    for (const GameVariableDefinition& def : doc.globalVariables) {
        nlohmann::json entry{
            {"key", def.key},
            {"type", def.type == GameVariableDefinition::Type::Number ? "number"
                : def.type == GameVariableDefinition::Type::Boolean ? "boolean" : "string"},
        };
        std::visit([&entry](const auto& value) { entry["initialValue"] = value; },
                   def.initialValue);
        if (!def.description.empty()) entry["description"] = def.description;
        globalVariables.push_back(std::move(entry));
    }

    nlohmann::json objectTypes = nlohmann::json::array();
    for (const auto& [id, def] : doc.objectTypes) {
        objectTypes.push_back(objectTypeToJson(id, def));
    }

    nlohmann::json imageAssets = nlohmann::json::array();
    for (const ImageAssetDef& asset : doc.imageAssets) {
        imageAssets.push_back(nlohmann::json{
            {"assetId", asset.assetId},
            {"name", asset.name.empty() ? asset.assetId : asset.name},
            {"sourcePath", asset.sourcePath},
        });
    }

    nlohmann::json tilesets = nlohmann::json::array();
    for (const TilesetAsset& asset : doc.tilesets) {
        nlohmann::json tiles = nlohmann::json::array();
        for (const TileDefinition& tile : asset.tiles) {
            tiles.push_back(nlohmann::json{
                {"id", tile.id}, {"x", tile.x}, {"y", tile.y},
                {"width", tile.width}, {"height", tile.height},
            });
        }
        tilesets.push_back(nlohmann::json{
            {"assetId", asset.assetId},
            {"name", asset.name.empty() ? asset.assetId : asset.name},
            {"imageAssetId", asset.imageAssetId},
            {"slicing", nlohmann::json{
                {"tileWidth", asset.slicing.tileWidth},
                {"tileHeight", asset.slicing.tileHeight},
                {"marginX", asset.slicing.marginX},
                {"marginY", asset.slicing.marginY},
                {"spacingX", asset.slicing.spacingX},
                {"spacingY", asset.slicing.spacingY},
            }},
            {"tiles", std::move(tiles)},
        });
    }

    nlohmann::json spriteAnimationAssets = nlohmann::json::array();
    for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
        nlohmann::json clips = nlohmann::json::array();
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            clips.push_back(animationClipToJson(clip));
        }
        nlohmann::json frames = nlohmann::json::array();
        for (const SpriteFrameDef& frame : asset.frames) {
            frames.push_back(animationFrameToJson(frame));
        }
        spriteAnimationAssets.push_back(nlohmann::json{
            {"id", asset.id},
            {"name", asset.name},
            {"sourceImageAssetId", asset.sourceImageAssetId},
            {"frames", std::move(frames)},
            {"clips", std::move(clips)},
        });
    }

    nlohmann::json audioAssets = nlohmann::json::array();
    for (const AudioAssetDef& asset : doc.audioAssets) {
        const bool linkedGeneratedOutput =
            audioIsLinkedGeneratedOutput(doc, asset.assetId);
        nlohmann::json entry{
            {"assetId", asset.assetId},
            {"name", linkedGeneratedOutput
                ? std::string{} : (asset.name.empty() ? asset.assetId : asset.name)},
            {"sourcePath", asset.sourcePath},
            {"loadMode", audioLoadModeToString(asset.loadMode)},
        };
        if (asset.generatedFromSfxId && !asset.generatedFromSfxId->empty())
            entry["generatedFromSfxId"] = *asset.generatedFromSfxId;
        audioAssets.push_back(std::move(entry));
    }

    nlohmann::json generatedSfx = nlohmann::json::array();
    for (const artcade::sfx::GeneratedSfxDef& definition : doc.generatedSfx) {
        const auto encoded = artcade::sfx::serializeRecipeJson(definition, -1);
        if (!encoded.ok()) {
            return SerializeResult::failure(
                "Generated SFX recipe could not be serialized: " + encoded.error().message);
        }
        generatedSfx.push_back(nlohmann::json::parse(encoded.value()));
    }

    nlohmann::json fontAssets = nlohmann::json::array();
    for (const FontAssetDef& asset : doc.fontAssets) {
        fontAssets.push_back(nlohmann::json{
            {"assetId", asset.assetId},
            {"name", asset.name.empty() ? asset.assetId : asset.name},
            {"sourcePath", asset.sourcePath},
            {"defaultPixelSize", asset.defaultPixelSize},
            {"glyphPreset", fontGlyphPresetToString(asset.glyphPreset)},
        });
    }

    nlohmann::json scriptAssets = nlohmann::json::array();
    for (const ScriptAssetDef& asset : doc.scriptAssets) {
        scriptAssets.push_back(nlohmann::json{
            {"assetId", asset.assetId},
            {"name", asset.name.empty() ? asset.assetId : asset.name},
            {"sourcePath", asset.sourcePath},
        });
    }

    nlohmann::json root{
        {"schemaVersion", kCurrentSchemaVersion},
        {"formatVersion", kCurrentSchemaVersion},
        {"projectName", doc.projectName},
        {"version", doc.version},
        {"activeSceneId", doc.activeSceneId},
        {"targetFPS", doc.targetFPS},
        {"mainScriptPath", doc.mainScriptPath},
        {"globalVariables", std::move(globalVariables)},
        {"scenes", std::move(scenes)},
        {"objectTypes", std::move(objectTypes)},
        {"imageAssets", std::move(imageAssets)},
        {"tilesets", std::move(tilesets)},
        {"spriteAnimationAssets", std::move(spriteAnimationAssets)},
        {"audioAssets", std::move(audioAssets)},
        {"generatedSfx", std::move(generatedSfx)},
        {"fontAssets", std::move(fontAssets)},
        {"scriptAssets", std::move(scriptAssets)},
    };
    return SerializeResult::success(root.dump(2));
}

DeserializeResult ProjectMigration::migrate(ProjectDocument document) {
    const int version = document.data().formatVersion;
    if (version < 0 || version > kCurrentSchemaVersion) {
        return DeserializeResult::failure("Unsupported project schema version");
    }
    if (version < kCurrentSchemaVersion) {
        ProjectDoc migrated = document.data();
        if (version < 4) migrateSpriteOwnershipToObjectTypes(migrated);
        if (version < 8) migrateGeneratedSfxAuthority(migrated);
        if (version < 9) migrateSpriteAnimationOwnershipV9(migrated);
        migrated.formatVersion = kCurrentSchemaVersion;
        return DeserializeResult::success(ProjectDocument{std::move(migrated)});
    }
    return DeserializeResult::success(std::move(document));
}

DeserializeResult ProjectValidator::validate(ProjectDocument document) {
    const ProjectDoc& data = document.data();

    for (const auto& [objectTypeId, type] : data.objectTypes) {
        if (type.logicBoard) {
            const auto diagnostics =
                Logic::validateBoard(objectTypeId, *type.logicBoard, &type, &data,
                                     Logic::ValidationMode::Authoring);
            const auto error = std::find_if(
                diagnostics.begin(), diagnostics.end(),
                [](const Logic::LogicDiagnostic& diagnostic) {
                    return diagnostic.severity == Logic::DiagnosticSeverity::Error;
                });
            if (error != diagnostics.end()) {
                return DeserializeResult::failure(
                    error->code + ": " + error->message);
            }
        }
        if (type.spriteRenderer) {
            const AssetId& imageId = type.spriteRenderer->imageAssetId;
            if (!imageId.empty() && !document.hasImageAsset(imageId)) {
                return DeserializeResult::failure(
                    "Object Type SpriteRenderer references a missing image asset");
            }
        } else if (type.spriteAnimator) {
            return DeserializeResult::failure(
                "Object Type SpriteAnimator requires SpriteRenderer");
        }
        if (type.spriteAnimator) {
            const AssetId& animationId = type.spriteAnimator->animationAssetId;
            if (animationId.empty()) {
                return DeserializeResult::failure(
                    "Object Type SpriteAnimator requires an animation asset");
            }
            if (!document.hasSpriteAnimationAsset(animationId)) {
                return DeserializeResult::failure(
                    "Object Type SpriteAnimator references a missing animation asset");
            }
            const SpriteAnimationAssetDef* asset =
                document.findSpriteAnimationAsset(animationId);
            if (!type.spriteAnimator->defaultClipId.empty()
                && !(asset && assetOwnsClip(*asset, type.spriteAnimator->defaultClipId))) {
                return DeserializeResult::failure(
                    "Object Type SpriteAnimator defaultClipId must belong to its animation asset");
            }
            if (!NumericValidation::isValid(*type.spriteAnimator)) {
                return DeserializeResult::failure(
                    "Object Type SpriteAnimator playbackSpeed must be positive");
            }
        }
    }

    if (!NumericValidation::isFinite(data.targetFPS) || data.targetFPS <= 0.f) {
        return DeserializeResult::failure("Project targetFPS must be positive");
    }
    if (!NumericValidation::isFinite(data.world.gravity)
        || !NumericValidation::isPositive(data.world.pixelsPerMeter)
        || !NumericValidation::isNonNegative(data.world.timeScale)) {
        return DeserializeResult::failure("Project runtime settings contain invalid values");
    }

    std::unordered_set<AssetId> imageAssetIds;
    for (const ImageAssetDef& asset : data.imageAssets) {
        if (asset.assetId.empty()) {
            return DeserializeResult::failure("Image asset id cannot be empty");
        }
        if (!imageAssetIds.insert(asset.assetId).second) {
            return DeserializeResult::failure("Duplicate image asset id");
        }
        if (!isPortableAssetPath(asset.sourcePath)) {
            return DeserializeResult::failure("Image asset path must be relative");
        }
    }

    std::unordered_set<AssetId> tilesetAssetIds;
    for (const TilesetAsset& asset : data.tilesets) {
        if (asset.assetId.empty()) {
            return DeserializeResult::failure("Tileset asset id cannot be empty");
        }
        if (!tilesetAssetIds.insert(asset.assetId).second) {
            return DeserializeResult::failure("Duplicate tileset asset id");
        }
        if (asset.imageAssetId.empty() || !imageAssetIds.count(asset.imageAssetId)) {
            return DeserializeResult::failure("Tileset asset references an unknown image asset");
        }
        if (asset.slicing.tileWidth <= 0 || asset.slicing.tileHeight <= 0) {
            return DeserializeResult::failure("Tileset tile size must be positive");
        }
        if (asset.slicing.marginX < 0 || asset.slicing.marginY < 0
            || asset.slicing.spacingX < 0 || asset.slicing.spacingY < 0) {
            return DeserializeResult::failure("Tileset margin/spacing cannot be negative");
        }
        std::unordered_set<std::string> tileIds;
        for (const TileDefinition& tile : asset.tiles) {
            if (tile.id.empty()) {
                return DeserializeResult::failure("Tile id cannot be empty");
            }
            if (!tileIds.insert(tile.id).second) {
                return DeserializeResult::failure("Duplicate tile id within a tileset");
            }
        }
    }

    std::unordered_set<AssetId> audioAssetIds;
    for (const AudioAssetDef& asset : data.audioAssets) {
        if (asset.assetId.empty()) {
            return DeserializeResult::failure("Audio asset id cannot be empty");
        }
        if (!audioAssetIds.insert(asset.assetId).second) {
            return DeserializeResult::failure("Duplicate audio asset id");
        }
        if (!asset.name.empty()
            && normalizeAudioDisplayName(asset.name) != asset.name) {
            return DeserializeResult::failure("Audio asset name must be normalized");
        }
        if (!isPortableAssetPath(asset.sourcePath)) {
            return DeserializeResult::failure("Audio asset path must be relative");
        }
    }

    std::unordered_set<std::string> generatedSfxIds;
    std::unordered_set<std::string> generatedSfxNames;
    for (const artcade::sfx::GeneratedSfxDef& definition : data.generatedSfx) {
        if (definition.schemaVersion != 1u) {
            return DeserializeResult::failure("Unsupported Generated SFX schema version");
        }
        if (definition.id.empty() || definition.name.empty()) {
            return DeserializeResult::failure("Generated SFX id and name cannot be empty");
        }
        const std::string normalizedGeneratedName =
            normalizeAudioDisplayName(definition.name);
        if (normalizedGeneratedName != definition.name) {
            return DeserializeResult::failure(
                "Generated SFX name must be normalized");
        }
        if (!generatedSfxIds.insert(definition.id).second) {
            return DeserializeResult::failure("Duplicate Generated SFX id");
        }
        std::string foldedName = normalizedGeneratedName;
        std::transform(foldedName.begin(), foldedName.end(), foldedName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!generatedSfxNames.insert(std::move(foldedName)).second) {
            return DeserializeResult::failure("Duplicate Generated SFX name");
        }
        const auto recipeValidation = artcade::sfx::SfxSynthesizer::validate(definition.recipe);
        if (!recipeValidation.ok()) {
            return DeserializeResult::failure(
                "Generated SFX '" + definition.name + "' is invalid: "
                + recipeValidation.error().message);
        }
        if (!definition.outputPath.empty() && !isPortableAssetPath(definition.outputPath)) {
            return DeserializeResult::failure("Generated SFX output path must be relative");
        }
    }

    const GeneratedSfxAuthorityValidation generatedAuthority =
        validateGeneratedSfxAuthority(data);
    if (!generatedAuthority.ok)
        return DeserializeResult::failure(generatedAuthority.error);

    std::unordered_set<std::string> audioDisplayNames;
    for (const artcade::sfx::GeneratedSfxDef& definition : data.generatedSfx) {
        std::string normalized = normalizeAudioDisplayName(definition.name);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        audioDisplayNames.insert(std::move(normalized));
    }
    for (const AudioAssetDef& asset : data.audioAssets) {
        if (audioIsLinkedGeneratedOutput(data, asset.assetId)) continue;
        std::string normalized = normalizeAudioDisplayName(
            asset.name.empty() ? asset.assetId : asset.name);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!audioDisplayNames.insert(std::move(normalized)).second) {
            return DeserializeResult::failure(
                "Audio display names must be unique across audio and Generated SFX");
        }
    }

    std::unordered_set<AssetId> fontAssetIds;
    for (const FontAssetDef& asset : data.fontAssets) {
        if (asset.assetId.empty()) {
            return DeserializeResult::failure("Font asset id cannot be empty");
        }
        if (!fontAssetIds.insert(asset.assetId).second) {
            return DeserializeResult::failure("Duplicate font asset id");
        }
        if (!isPortableAssetPath(asset.sourcePath)) {
            return DeserializeResult::failure("Font asset path must be relative");
        }
        if (asset.defaultPixelSize <= 0) {
            return DeserializeResult::failure("Font default pixel size must be positive");
        }
    }

    std::unordered_set<AssetId> scriptAssetIds;
    std::unordered_set<std::string> scriptSourcePaths;
    for (const ScriptAssetDef& asset : data.scriptAssets) {
        if (asset.assetId.empty() || asset.name.empty()) {
            return DeserializeResult::failure("Script asset id and name cannot be empty");
        }
        if (!scriptAssetIds.insert(asset.assetId).second) {
            return DeserializeResult::failure("Duplicate script asset id");
        }
        if (!isPortableAssetPath(asset.sourcePath)) {
            return DeserializeResult::failure("Script asset path must be relative");
        }
        std::string extension = std::filesystem::u8path(asset.sourcePath).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".lua") {
            return DeserializeResult::failure("Script asset source must be a .lua file");
        }
        std::string normalizedPath = std::filesystem::u8path(asset.sourcePath)
            .lexically_normal().generic_u8string();
        std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!scriptSourcePaths.insert(std::move(normalizedPath)).second) {
            return DeserializeResult::failure("Duplicate script asset source path");
        }
    }

    for (const auto& [objectTypeId, type] : data.objectTypes) {
        if (!type.scripts) continue;
        if (type.scripts->attachments.empty()) {
            return DeserializeResult::failure(
                "Object Type Script component cannot be empty");
        }
        std::unordered_set<ScriptAttachmentId> attachmentIds;
        for (const ScriptAttachmentDef& attachment : type.scripts->attachments) {
            if (attachment.id.empty()) {
                return DeserializeResult::failure(
                    "Script attachment id cannot be empty");
            }
            if (!attachmentIds.insert(attachment.id).second) {
                return DeserializeResult::failure(
                    "Duplicate Script attachment id within Object Type " + objectTypeId);
            }
            if (attachment.scriptAssetId.empty()
                || scriptAssetIds.count(attachment.scriptAssetId) == 0) {
                return DeserializeResult::failure(
                    "Script attachment references an unknown Script Asset");
            }
        }
    }

    std::unordered_set<SceneId> sceneIds;
    for (const auto& [id, scene] : data.scenes) {
        if (id.empty() || scene.id.empty()) {
            return DeserializeResult::failure("Scene id cannot be empty");
        }
        if (!sceneIds.insert(scene.id).second) {
            return DeserializeResult::failure("Duplicate scene id");
        }
        if (id != scene.id) {
            return DeserializeResult::failure("Scene map key does not match scene id");
        }
        if (!NumericValidation::isPositive(scene.worldSize)) {
            return DeserializeResult::failure("Scene worldSize must be positive");
        }
        if (!NumericValidation::isPositive(scene.viewportSize)) {
            return DeserializeResult::failure("Scene viewportSize must be positive");
        }
        if (!NumericValidation::isFinite(scene.cameraStart)) {
            return DeserializeResult::failure("Scene cameraStart must be finite");
        }
        if (!NumericValidation::isFinite(scene.backgroundColor)) {
            return DeserializeResult::failure("Scene backgroundColor must be finite");
        }

        // Per-scene layer invariants (only when the scene declares layers; a
        // legacy/in-memory scene with none renders its instances directly).
        std::unordered_set<std::string> layerIds;
        if (!scene.layers.empty()) {
            for (const SceneLayerDef& layer : scene.layers) {
                if (layer.id.empty()) {
                    return DeserializeResult::failure("Scene layer id cannot be empty");
                }
                if (!layerIds.insert(layer.id).second) {
                    return DeserializeResult::failure("Duplicate scene layer id");
                }
            }
            if (scene.defaultLayerId.empty() || layerIds.count(scene.defaultLayerId) == 0) {
                return DeserializeResult::failure(
                    "Scene defaultLayerId must reference an existing layer");
            }
        }

        std::unordered_set<EntityId> entityIds;
        for (const SceneInstanceDef& instance : scene.instances) {
            if (instance.id == INVALID_ENTITY) {
                return DeserializeResult::failure("Entity id cannot be zero");
            }
            if (!entityIds.insert(instance.id).second) {
                return DeserializeResult::failure("Duplicate entity id in scene");
            }
            if (instance.objectTypeId.empty()) {
                return DeserializeResult::failure("Entity objectTypeId cannot be empty");
            }
            if (!NumericValidation::isFinite(instance.transform)) {
                return DeserializeResult::failure("Entity transform must be finite");
            }
            // When the scene has layers, an instance must reference a real one.
            if (!scene.layers.empty() && !instance.layerId.empty()
                && layerIds.count(instance.layerId) == 0) {
                return DeserializeResult::failure("Instance references a missing scene layer");
            }
            // When the project defines an object-type catalog, every instance must
            // reference an existing type (a dangling reference is rejected). A
            // catalog-less minimal project leaves objectTypeId as a free label.
            if (!data.objectTypes.empty()
                && data.objectTypes.find(instance.objectTypeId) == data.objectTypes.end()) {
                return DeserializeResult::failure("Instance references a missing object type");
            }
            ResolvedSpritePresentation presentation;
            const auto typeIt = data.objectTypes.find(instance.objectTypeId);
            if (typeIt != data.objectTypes.end()) {
                if (instance.spriteRendererOverride && !typeIt->second.spriteRenderer) {
                    return DeserializeResult::failure(
                        "SpriteRenderer override requires the Object Type capability");
                }
                if (instance.spriteAnimatorOverride && !typeIt->second.spriteAnimator) {
                    return DeserializeResult::failure(
                        "SpriteAnimator override requires the Object Type capability");
                }
                if (instance.spriteAnimatorOverride
                    && instance.spriteAnimatorOverride->playbackSpeed
                    && !NumericValidation::isPositive(
                        *instance.spriteAnimatorOverride->playbackSpeed)) {
                    return DeserializeResult::failure(
                        "SpriteAnimator override playbackSpeed must be positive");
                }
                presentation = resolveSpritePresentation(typeIt->second, instance);
            }
            // Validate the exact presentation Edit and Play will consume.
            if (presentation.renderer.has_value()) {
                const AssetId& imageId = presentation.renderer->imageAssetId;
                if (!imageId.empty() && !document.hasImageAsset(imageId)) {
                    return DeserializeResult::failure(
                        "Sprite renderer references a missing image asset");
                }
            } else if (presentation.animator.has_value()) {
                return DeserializeResult::failure(
                    "SpriteAnimator requires a sprite renderer");
            }
            if (presentation.animator.has_value()) {
                const AssetId& animationId = presentation.animator->animationAssetId;
                if (animationId.empty()) {
                    return DeserializeResult::failure(
                        "SpriteAnimator requires an animation asset");
                }
                if (!document.hasSpriteAnimationAsset(animationId)) {
                    return DeserializeResult::failure(
                        "SpriteAnimator references a missing animation asset");
                }
                const SpriteAnimationAssetDef* asset =
                    document.findSpriteAnimationAsset(animationId);
                if (!presentation.animator->defaultClipId.empty()
                    && !(asset && assetOwnsClip(
                            *asset, presentation.animator->defaultClipId))) {
                    return DeserializeResult::failure(
                        "SpriteAnimator defaultClipId must belong to its animation asset");
                }
                if (!NumericValidation::isValid(*presentation.animator)) {
                    return DeserializeResult::failure(
                        "SpriteAnimator playbackSpeed must be positive");
                }
            }
            if (instance.tilemap.has_value()) {
                if (const auto err = validateTilemapComponent(document, *instance.tilemap)) {
                    return DeserializeResult::failure(*err);
                }
            }
        }
    }

    std::unordered_set<AssetId> animationIds;
    for (const SpriteAnimationAssetDef& asset : data.spriteAnimationAssets) {
        if (asset.id.empty()) {
            return DeserializeResult::failure("Sprite animation asset id cannot be empty");
        }
        if (!animationIds.insert(asset.id).second) {
            return DeserializeResult::failure("Duplicate sprite animation asset id");
        }
        if (!asset.sourceImageAssetId.empty()
            && !document.hasImageAsset(asset.sourceImageAssetId)) {
            return DeserializeResult::failure(
                "Sprite animation asset references a missing source image");
        }
        std::unordered_set<std::string> frameIds;
        for (const SpriteFrameDef& frame : asset.frames) {
            if (frame.id.empty()) {
                return DeserializeResult::failure("Animation frame id cannot be empty");
            }
            if (!frameIds.insert(frame.id).second) {
                return DeserializeResult::failure("Duplicate animation frame id");
            }
            if (frame.x < 0 || frame.y < 0 || frame.width <= 0 || frame.height <= 0) {
                return DeserializeResult::failure("Animation sourceRect is invalid");
            }
        }
        std::unordered_set<std::string> clipIds;
        std::unordered_set<std::string> clipNames;
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id.empty()) {
                return DeserializeResult::failure("Animation clip id cannot be empty");
            }
            if (clip.name.empty()) {
                return DeserializeResult::failure("Animation clip name cannot be empty");
            }
            if (!clipIds.insert(clip.id).second) {
                return DeserializeResult::failure("Duplicate animation clip id");
            }
            if (!clipNames.insert(clip.name).second) {
                return DeserializeResult::failure("Duplicate animation clip name");
            }
            if (!Animation::isValidAnimationFps(clip.framesPerSecond)) {
                return DeserializeResult::failure(
                    "Animation clip FPS must be finite, > 0, and <= "
                    + std::to_string(static_cast<int>(Animation::kMaxAnimationFps)));
            }
            for (const SpriteFrameId& frameId : clip.frameIds) {
                if (frameIds.find(frameId) == frameIds.end()) {
                    return DeserializeResult::failure(
                        "Animation clip frameIds must resolve against the asset frame pool");
                }
            }
        }
    }

    // Legacy SpriteComponent contributes fillColor only; source ownership was
    // migrated to the v4 SpriteRenderer capability above.
    for (const auto& [typeId, def] : data.objectTypes) {
        (void)typeId;
        if (!NumericValidation::isFinite(def.sprite.fillColor)) {
            return DeserializeResult::failure("Object type sprite fillColor must be finite");
        }
        if (def.boxCollider2D.has_value()) {
            if (!NumericValidation::isValid(*def.boxCollider2D)) {
                return DeserializeResult::failure("BoxCollider2D size must be positive");
            }
        }
        if (def.linearMover.has_value()) {
            if (!NumericValidation::isValid(*def.linearMover)) {
                return DeserializeResult::failure("LinearMover has invalid direction or speed");
            }
        }
        if (def.topDownController.has_value()) {
            const TopDownControllerComponent& tdc = *def.topDownController;
            if (!NumericValidation::isValid(tdc)) {
                return DeserializeResult::failure("TopDownController has invalid speed");
            }
        }
        if (def.platformerController.has_value()) {
            const PlatformerControllerComponent& pc = *def.platformerController;
            if (!NumericValidation::isValid(pc)) {
                return DeserializeResult::failure("PlatformerController has invalid values");
            }
        }
        // One movement writer per object type is a project invariant, not just a
        // runtime convenience: reject a file that carries several rather than
        // silently letting materialize pick one by priority.
        const int movementDrivers = (def.linearMover.has_value() ? 1 : 0)
                                  + (def.topDownController.has_value() ? 1 : 0)
                                  + (def.platformerController.has_value() ? 1 : 0);
        if (movementDrivers > 1) {
            return DeserializeResult::failure(
                "Object type has multiple movement drivers (only one is allowed)");
        }
        if (movementDrivers > 0
            && def.boxCollider2D.has_value()
            && def.boxCollider2D->mode == BoxColliderMode::OneWayPlatform) {
            return DeserializeResult::failure(
                "OneWayPlatform does not support movement drivers");
        }
    }

    if (!data.scenes.empty()) {
        if (data.activeSceneId.empty()) {
            return DeserializeResult::failure("startSceneId cannot be empty when scenes exist");
        }
        if (data.scenes.find(data.activeSceneId) == data.scenes.end()) {
            return DeserializeResult::failure("startSceneId references a missing scene");
        }
    }

    return DeserializeResult::success(std::move(document));
}

} // namespace ArtCade::EditorNative
