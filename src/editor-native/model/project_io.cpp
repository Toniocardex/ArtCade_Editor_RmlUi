#include "editor-native/model/project_io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <unordered_set>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

// v2: sprite animation source moved from asset-level imageId to per-clip imageId
// (each clip owns its sheet), plus asset defaultClipId.
constexpr int kCurrentSchemaVersion = 2;

nlohmann::json vec2ToJson(const Vec2& v) {
    return nlohmann::json{{"x", v.x}, {"y", v.y}};
}

nlohmann::json vec3ToJson(const Vec3& v) {
    return nlohmann::json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

std::string readString(const nlohmann::json& object, const char* camel,
                       const char* snake = nullptr,
                       const std::string& fallback = {}) {
    if (object.contains(camel) && object[camel].is_string()) {
        return object[camel].get<std::string>();
    }
    if (snake && object.contains(snake) && object[snake].is_string()) {
        return object[snake].get<std::string>();
    }
    return fallback;
}

float readFloat(const nlohmann::json& object, const char* key, float fallback) {
    if (object.contains(key) && object[key].is_number()) {
        return object[key].get<float>();
    }
    return fallback;
}

Vec2 readVec2(const nlohmann::json& value, Vec2 fallback = {}) {
    if (!value.is_object()) return fallback;
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
    return !sourcePath.empty()
        && !std::filesystem::path(sourcePath).is_absolute();
}

Vec4 readVec4(const nlohmann::json& value, Vec4 fallback = {}) {
    if (!value.is_object()) return fallback;
    return Vec4{
        readFloat(value, "r", fallback.r),
        readFloat(value, "g", fallback.g),
        readFloat(value, "b", fallback.b),
        readFloat(value, "a", fallback.a),
    };
}

Vec3 readVec3(const nlohmann::json& value, Vec3 fallback = {}) {
    if (!value.is_object()) return fallback;
    return Vec3{
        readFloat(value, "x", fallback.x),
        readFloat(value, "y", fallback.y),
        readFloat(value, "z", fallback.z),
    };
}

Transform readTransform(const nlohmann::json& value) {
    Transform transform;
    if (!value.is_object()) return transform;
    if (value.contains("position")) transform.position = readVec2(value["position"]);
    if (value.contains("scale")) transform.scale = readVec2(value["scale"], {1.f, 1.f});
    transform.rotation = readFloat(value, "rotation", 0.f);
    return transform;
}

bool readInstance(const nlohmann::json& value, SceneInstanceDef& out) {
    if (!value.is_object()) return false;
    out = SceneInstanceDef{};
    out.id = value.value("id", 0u);
    out.objectTypeId = readString(value, "objectTypeId", "object_type_id");
    out.instanceName = readString(value, "instanceName", "instance_name");
    if (value.contains("transform")) out.transform = readTransform(value["transform"]);
    if (value.contains("visible") && value["visible"].is_boolean()) {
        out.visible = value["visible"].get<bool>();
    }
    out.layerId = readString(value, "layerId", "layer_id");
    if (value.contains("spriteRenderer") && value["spriteRenderer"].is_object()) {
        const auto& sr = value["spriteRenderer"];
        SpriteRendererComponent component;
        component.imageAssetId = readString(sr, "imageAssetId", "image_asset_id");
        component.animationAssetId = readString(sr, "animationAssetId", "animation_asset_id");
        component.visible = sr.value("visible", true);
        out.spriteRenderer = component;
    }
    if (value.contains("spriteAnimator") && value["spriteAnimator"].is_object()) {
        const auto& sa = value["spriteAnimator"];
        SpriteAnimatorComponent component;
        component.initialClipId = readString(sa, "initialClipId", "initial_clip_id");
        component.autoPlay = sa.value("autoPlay", true);
        component.playbackSpeed = readFloat(sa, "playbackSpeed", 1.f);
        out.spriteAnimator = component;
    }
    return out.id != INVALID_ENTITY && !out.objectTypeId.empty();
}

SceneDef readScene(const nlohmann::json& value, const SceneId& fallbackId) {
    SceneDef scene;
    if (!value.is_object()) return scene;
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
    if (value.contains("instances") && value["instances"].is_array()) {
        for (const auto& item : value["instances"]) {
            SceneInstanceDef instance;
            if (readInstance(item, instance)) scene.instances.push_back(std::move(instance));
        }
    }
    if (value.contains("layers") && value["layers"].is_array()) {
        for (const auto& item : value["layers"]) {
            if (!item.is_object()) continue;
            SceneLayerDef layer;
            layer.id = readString(item, "id", nullptr);
            layer.name = readString(item, "name", nullptr, layer.id);
            if (!layer.id.empty()) scene.layers.push_back(layer);
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
    if (!root.contains("scenes")) return;
    const auto& scenes = root["scenes"];
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
    if (instance.spriteRenderer.has_value()) {
        json["spriteRenderer"] = nlohmann::json{
            {"imageAssetId", instance.spriteRenderer->imageAssetId},
            {"animationAssetId", instance.spriteRenderer->animationAssetId},
            {"visible", instance.spriteRenderer->visible},
        };
    }
    if (instance.spriteAnimator.has_value()) {
        json["spriteAnimator"] = nlohmann::json{
            {"initialClipId", instance.spriteAnimator->initialClipId},
            {"autoPlay", instance.spriteAnimator->autoPlay},
            {"playbackSpeed", instance.spriteAnimator->playbackSpeed},
        };
    }
    return json;
}

nlohmann::json animationFrameToJson(const SpriteAnimationFrameDef& frame) {
    return nlohmann::json{
        {"x", frame.x},
        {"y", frame.y},
        {"width", frame.width},
        {"height", frame.height},
    };
}

nlohmann::json animationClipToJson(const SpriteAnimationClipDef& clip) {
    nlohmann::json frames = nlohmann::json::array();
    for (const SpriteAnimationFrameDef& frame : clip.frames) {
        frames.push_back(animationFrameToJson(frame));
    }
    return nlohmann::json{
        {"id", clip.id},
        {"name", clip.name},
        {"imageId", clip.imageId},
        {"frames", std::move(frames)},
        {"framesPerSecond", clip.framesPerSecond},
        {"playbackMode", playbackModeToString(clip.playbackMode)},
    };
}

// Minimal object-type persistence: only the fields the native editor resolves
// or renders (id, name, visible, sprite asset + fill). The full EntityDef bag is
// deliberately not serialized by the spike.
nlohmann::json objectTypeToJson(const std::string& id, const EntityDef& def) {
    nlohmann::json json{
        {"id", id},
        {"name", def.name},
        {"visible", def.visible},
        {"sprite", nlohmann::json{
            {"spriteAssetId", def.sprite.spriteAssetId},
            {"fillColor", vec3ToJson(def.sprite.fillColor)},
        }},
    };
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
        layers.push_back(nlohmann::json{{"id", layer.id}, {"name", layer.name}});
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

} // namespace

DeserializeResult ProjectSerializer::deserialize(std::string_view source) {
    const nlohmann::json root =
        nlohmann::json::parse(source.begin(), source.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return DeserializeResult::failure("Project JSON is malformed");
    }

    ProjectDoc doc;
    doc.projectName = readString(root, "projectName", "project_name", "Untitled");
    doc.version = readString(root, "version", nullptr, "2.0.0");
    doc.licenseTier = readString(root, "licenseTier", "license_tier", "free");
    doc.targetFPS = readFloat(root, "targetFPS", 60.f);
    doc.activeSceneId = readString(root, "activeSceneId", "active_scene_id");
    doc.mainScriptPath = readString(root, "mainScriptPath", "main_script_path",
                                    "scripts/main.luac");
    doc.formatVersion = root.value("formatVersion", root.value("format_version", 0));
    readScenes(root, doc);

    if (root.contains("objectTypes") && root["objectTypes"].is_array()) {
        std::unordered_set<std::string> seenTypeIds;
        for (const auto& item : root["objectTypes"]) {
            if (!item.is_object()) continue;
            const std::string id = readString(item, "id", "className");
            if (id.empty()) continue;
            if (!seenTypeIds.insert(id).second) {
                return DeserializeResult::failure("Duplicate object type id");
            }
            EntityDef def;
            def.className = id;
            def.name = readString(item, "name", nullptr, id);
            def.visible = item.value("visible", true);
            if (item.contains("sprite") && item["sprite"].is_object()) {
                const auto& sprite = item["sprite"];
                def.sprite.spriteAssetId = readString(sprite, "spriteAssetId", "sprite_asset_id");
                if (sprite.contains("fillColor")) {
                    def.sprite.fillColor = readVec3(sprite["fillColor"], def.sprite.fillColor);
                }
            }
            if (item.contains("boxCollider2D") && item["boxCollider2D"].is_object()) {
                const auto& collider = item["boxCollider2D"];
                BoxCollider2DComponent component;
                if (collider.contains("offset")) {
                    component.offset = readVec2(collider["offset"], component.offset);
                }
                if (collider.contains("size")) {
                    component.size = readVec2(collider["size"], component.size);
                }
                if (collider.contains("enabled") && collider["enabled"].is_boolean()) {
                    component.enabled = collider["enabled"].get<bool>();
                }
                if (collider.contains("mode")) {
                    if (!collider["mode"].is_string()) {
                        return DeserializeResult::failure("BoxCollider2D mode is invalid");
                    }
                    const auto parsed = BoxColliderModeFromString(collider["mode"].get<std::string>());
                    if (!parsed.has_value()) {
                        return DeserializeResult::failure("BoxCollider2D mode is unknown");
                    }
                    if (collider.contains("isTrigger") && collider["isTrigger"].is_boolean()
                        && legacyTriggerMode(collider["isTrigger"].get<bool>()) != *parsed) {
                        return DeserializeResult::failure(
                            "BoxCollider2D mode conflicts with legacy isTrigger");
                    }
                    component.mode = *parsed;
                } else if (collider.contains("isTrigger") && collider["isTrigger"].is_boolean()) {
                    component.mode = legacyTriggerMode(collider["isTrigger"].get<bool>());
                }
                def.boxCollider2D = component;
            }
            if (item.contains("linearMover") && item["linearMover"].is_object()) {
                const auto& m = item["linearMover"];
                LinearMoverComponent component;
                component.directionX = m.value("directionX", component.directionX);
                component.directionY = m.value("directionY", component.directionY);
                component.speed = m.value("speed", component.speed);
                def.linearMover = component;
            }
            if (item.contains("topDownController") && item["topDownController"].is_object()) {
                const auto& t = item["topDownController"];
                TopDownControllerComponent component;
                component.maxSpeed = t.value("maxSpeed", component.maxSpeed);
                component.acceleration = t.value("acceleration", component.acceleration);
                component.friction = t.value("friction", component.friction);
                component.fourDirections = t.value("fourDirections", component.fourDirections);
                def.topDownController = component;
            }
            if (item.contains("platformerController") && item["platformerController"].is_object()) {
                const auto& p = item["platformerController"];
                PlatformerControllerComponent component;   // others keep defaults
                component.maxSpeed      = p.value("moveSpeed", component.maxSpeed);
                component.jumpForce     = p.value("jumpSpeed", component.jumpForce);
                component.customGravity = p.value("gravity", component.customGravity);
                def.platformerController = component;
            }
            doc.objectTypes.emplace(id, std::move(def));
        }
    }

    if (root.contains("imageAssets") && root["imageAssets"].is_array()) {
        for (const auto& item : root["imageAssets"]) {
            if (!item.is_object()) continue;
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            if (assetId.empty()) continue;
            ImageAssetDef asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.sourcePath = readAssetPath(item);
            doc.imageAssets.push_back(std::move(asset));
        }
    }

    if (root.contains("spriteAnimationAssets")
        && root["spriteAnimationAssets"].is_array()) {
        for (const auto& item : root["spriteAnimationAssets"]) {
            if (!item.is_object()) continue;
            SpriteAnimationAssetDef asset;
            asset.id = readString(item, "id", "asset_id");
            asset.name = readString(item, "name", nullptr, asset.id);
            if (asset.id.empty()) continue;
            // Backfill for pre-per-clip files: an old asset-level imageId is the
            // source for every clip that doesn't carry its own (single-authority
            // migration; the model itself never keeps an asset-level image).
            const std::string legacyAssetImage = readString(item, "imageId", "image_id");
            asset.defaultClipId = readString(item, "defaultClipId", "default_clip_id");
            if (item.contains("clips") && item["clips"].is_array()) {
                for (const auto& clipJson : item["clips"]) {
                    if (!clipJson.is_object()) continue;
                    SpriteAnimationClipDef clip;
                    clip.id = readString(clipJson, "id", nullptr);
                    clip.name = readString(clipJson, "name", nullptr, clip.id);
                    clip.imageId = readString(clipJson, "imageId", "image_id");
                    if (clip.imageId.empty()) clip.imageId = legacyAssetImage;
                    clip.framesPerSecond =
                        readFloat(clipJson, "framesPerSecond", clip.framesPerSecond);
                    const std::string playback =
                        readString(clipJson, "playbackMode", nullptr, "loop");
                    const auto mode = playbackModeFromString(playback);
                    if (!mode.has_value()) {
                        return DeserializeResult::failure("Animation clip playbackMode is unknown");
                    }
                    clip.playbackMode = *mode;
                    if (clipJson.contains("frames") && clipJson["frames"].is_array()) {
                        for (const auto& frameJson : clipJson["frames"]) {
                            if (!frameJson.is_object()) continue;
                            SpriteAnimationFrameDef frame;
                            frame.x = frameJson.value("x", 0);
                            frame.y = frameJson.value("y", 0);
                            frame.width = frameJson.value("width", 0);
                            frame.height = frameJson.value("height", 0);
                            clip.frames.push_back(frame);
                        }
                    }
                    asset.clips.push_back(std::move(clip));
                }
            }
            // Normalize the default clip to a real one (first) when unset/dangling.
            const bool validDefault = !asset.defaultClipId.empty()
                && std::any_of(asset.clips.begin(), asset.clips.end(),
                               [&](const SpriteAnimationClipDef& c) {
                                   return c.id == asset.defaultClipId;
                               });
            if (!validDefault) {
                asset.defaultClipId = asset.clips.empty() ? std::string()
                                                          : asset.clips.front().id;
            }
            doc.spriteAnimationAssets.push_back(std::move(asset));
        }
    }

    if (root.contains("audioAssets") && root["audioAssets"].is_array()) {
        for (const auto& item : root["audioAssets"]) {
            if (!item.is_object()) continue;
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            if (assetId.empty()) continue;
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
            doc.audioAssets.push_back(std::move(asset));
        }
    }

    if (root.contains("fontAssets") && root["fontAssets"].is_array()) {
        for (const auto& item : root["fontAssets"]) {
            if (!item.is_object()) continue;
            std::string assetId = readString(item, "assetId", "asset_id");
            if (assetId.empty()) assetId = readString(item, "id", nullptr);
            if (assetId.empty()) continue;
            FontAssetDef asset;
            asset.assetId = assetId;
            asset.name = readString(item, "name", nullptr, assetId);
            asset.sourcePath = readAssetPath(item);
            asset.defaultPixelSize = item.value("defaultPixelSize", 32);
            const std::string preset = readString(item, "glyphPreset", nullptr, "european");
            const auto parsed = fontGlyphPresetFromString(preset);
            if (!parsed.has_value()) {
                return DeserializeResult::failure("Font asset glyphPreset is unknown");
            }
            asset.glyphPreset = *parsed;
            doc.fontAssets.push_back(std::move(asset));
        }
    }

    return DeserializeResult::success(ProjectDocument{std::move(doc)});
}

SerializeResult ProjectSerializer::serialize(const ProjectDocument& document) {
    const ProjectDoc& doc = document.data();
    nlohmann::json scenes = nlohmann::json::array();
    for (const auto& [_, scene] : doc.scenes) {
        scenes.push_back(sceneToJson(scene));
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

    nlohmann::json spriteAnimationAssets = nlohmann::json::array();
    for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
        nlohmann::json clips = nlohmann::json::array();
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            clips.push_back(animationClipToJson(clip));
        }
        spriteAnimationAssets.push_back(nlohmann::json{
            {"id", asset.id},
            {"name", asset.name},
            {"defaultClipId", asset.defaultClipId},
            {"clips", std::move(clips)},
        });
    }

    nlohmann::json audioAssets = nlohmann::json::array();
    for (const AudioAssetDef& asset : doc.audioAssets) {
        audioAssets.push_back(nlohmann::json{
            {"assetId", asset.assetId},
            {"name", asset.name.empty() ? asset.assetId : asset.name},
            {"sourcePath", asset.sourcePath},
            {"loadMode", audioLoadModeToString(asset.loadMode)},
        });
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

    nlohmann::json root{
        {"schemaVersion", kCurrentSchemaVersion},
        {"formatVersion", kCurrentSchemaVersion},
        {"projectName", doc.projectName},
        {"version", doc.version},
        {"activeSceneId", doc.activeSceneId},
        {"targetFPS", doc.targetFPS},
        {"mainScriptPath", doc.mainScriptPath},
        {"scenes", std::move(scenes)},
        {"objectTypes", std::move(objectTypes)},
        {"imageAssets", std::move(imageAssets)},
        {"spriteAnimationAssets", std::move(spriteAnimationAssets)},
        {"audioAssets", std::move(audioAssets)},
        {"fontAssets", std::move(fontAssets)},
    };
    return SerializeResult::success(root.dump(2));
}

DeserializeResult ProjectMigration::migrate(ProjectDocument document) {
    const int version = document.data().formatVersion;
    if (version < 0 || version > kCurrentSchemaVersion) {
        return DeserializeResult::failure("Unsupported project schema version");
    }
    return DeserializeResult::success(std::move(document));
}

DeserializeResult ProjectValidator::validate(ProjectDocument document) {
    const ProjectDoc& data = document.data();

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

    std::unordered_set<AssetId> audioAssetIds;
    for (const AudioAssetDef& asset : data.audioAssets) {
        if (asset.assetId.empty()) {
            return DeserializeResult::failure("Audio asset id cannot be empty");
        }
        if (!audioAssetIds.insert(asset.assetId).second) {
            return DeserializeResult::failure("Duplicate audio asset id");
        }
        if (!isPortableAssetPath(asset.sourcePath)) {
            return DeserializeResult::failure("Audio asset path must be relative");
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
            // A sprite renderer's asset reference must resolve to an image asset.
            if (instance.spriteRenderer.has_value()) {
                const AssetId& imageId = instance.spriteRenderer->imageAssetId;
                const AssetId& animationId = instance.spriteRenderer->animationAssetId;
                if (!imageId.empty() && !animationId.empty()) {
                    return DeserializeResult::failure(
                        "Sprite renderer cannot reference image and animation together");
                }
                if (!imageId.empty() && !document.hasImageAsset(imageId)) {
                    return DeserializeResult::failure(
                        "Sprite renderer references a missing image asset");
                }
                if (!animationId.empty() && !document.hasSpriteAnimationAsset(animationId)) {
                    return DeserializeResult::failure(
                        "Sprite renderer references a missing animation asset");
                }
                if (!animationId.empty() && !instance.spriteAnimator.has_value()) {
                    return DeserializeResult::failure(
                        "Animation sprite renderer requires SpriteAnimator");
                }
                if (animationId.empty() && instance.spriteAnimator.has_value()) {
                    return DeserializeResult::failure(
                        "SpriteAnimator requires an animation sprite source");
                }
            } else if (instance.spriteAnimator.has_value()) {
                return DeserializeResult::failure(
                    "SpriteAnimator requires a sprite renderer");
            }
            if (instance.spriteAnimator.has_value()) {
                const SpriteAnimationAssetDef* asset =
                    document.findSpriteAnimationAsset(instance.spriteRenderer->animationAssetId);
                bool ownsClip = false;
                if (asset) {
                    for (const SpriteAnimationClipDef& clip : asset->clips) {
                        if (clip.id == instance.spriteAnimator->initialClipId) {
                            ownsClip = true;
                            break;
                        }
                    }
                }
                if (!ownsClip) {
                    return DeserializeResult::failure(
                        "SpriteAnimator initialClipId must belong to its animation asset");
                }
                if (!std::isfinite(instance.spriteAnimator->playbackSpeed)
                    || instance.spriteAnimator->playbackSpeed <= 0.f) {
                    return DeserializeResult::failure(
                        "SpriteAnimator playbackSpeed must be positive");
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
            // Each clip owns its sheet; the referenced image must exist.
            if (!document.hasImageAsset(clip.imageId)) {
                return DeserializeResult::failure(
                    "Animation clip references a missing image asset");
            }
            if (!std::isfinite(clip.framesPerSecond) || clip.framesPerSecond <= 0.f) {
                return DeserializeResult::failure("Animation clip FPS must be positive");
            }
            for (const SpriteAnimationFrameDef& frame : clip.frames) {
                if (frame.x < 0 || frame.y < 0 || frame.width <= 0 || frame.height <= 0) {
                    return DeserializeResult::failure("Animation sourceRect is invalid");
                }
            }
        }
        // defaultClipId, when set, must name a clip of this asset.
        if (!asset.defaultClipId.empty() && clipIds.find(asset.defaultClipId) == clipIds.end()) {
            return DeserializeResult::failure(
                "Sprite animation defaultClipId does not name a clip of the asset");
        }
    }

    // An inherited sprite asset (on the object type) is validated like an override.
    for (const auto& [typeId, def] : data.objectTypes) {
        (void)typeId;
        const AssetId& assetId = def.sprite.spriteAssetId;
        if (!assetId.empty() && !document.hasImageAsset(assetId)) {
            return DeserializeResult::failure(
                "Object type sprite references a missing image asset");
        }
        if (def.boxCollider2D.has_value()) {
            const Vec2 size = def.boxCollider2D->size;
            if (!std::isfinite(def.boxCollider2D->offset.x)
                || !std::isfinite(def.boxCollider2D->offset.y)
                || !std::isfinite(size.x)
                || !std::isfinite(size.y)
                || size.x <= 0.f
                || size.y <= 0.f) {
                return DeserializeResult::failure("BoxCollider2D size must be positive");
            }
        }
        if (def.linearMover.has_value()) {
            if (!std::isfinite(def.linearMover->directionX)
                || !std::isfinite(def.linearMover->directionY)
                || !std::isfinite(def.linearMover->speed)
                || def.linearMover->speed < 0.f) {
                return DeserializeResult::failure("LinearMover has invalid direction or speed");
            }
        }
        if (def.topDownController.has_value()) {
            const TopDownControllerComponent& tdc = *def.topDownController;
            if (!std::isfinite(tdc.maxSpeed) || tdc.maxSpeed < 0.f
                || !std::isfinite(tdc.acceleration) || !std::isfinite(tdc.friction)) {
                return DeserializeResult::failure("TopDownController has invalid speed");
            }
        }
        if (def.platformerController.has_value()) {
            const PlatformerControllerComponent& pc = *def.platformerController;
            if (!std::isfinite(pc.maxSpeed) || pc.maxSpeed < 0.f
                || !std::isfinite(pc.jumpForce) || pc.jumpForce < 0.f
                || !std::isfinite(pc.customGravity) || pc.customGravity < 0.f) {
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
