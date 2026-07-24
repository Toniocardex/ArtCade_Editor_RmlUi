#include "project-current-format.h"
#include "project-global-variables-format.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace ArtCade::ProjectJson {
namespace {

std::string normalized_name(const std::string &value)
{
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    const std::size_t first = result.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
}

bool scene_has_layer(const SceneDef &scene, const std::string &layer_id)
{
    return std::any_of(scene.layers.begin(), scene.layers.end(), [&](const SceneLayerDef &layer) {
        return layer.id == layer_id;
    });
}

bool validate_scene_document(const SceneDef &scene, std::string &error_message)
{
    if (scene.id.empty()) {
        error_message = "A scene has an empty id.";
        return false;
    }
    if (scene.layers.empty()) {
        error_message = "Scene \"" + scene.id + "\" must contain at least one layer.";
        return false;
    }

    std::unordered_set<std::string> layer_ids;
    std::unordered_set<std::string> layer_names;
    for (const SceneLayerDef &layer : scene.layers) {
        if (layer.id.empty() || layer.name.empty()) {
            error_message = "Scene \"" + scene.id + "\" contains a layer without id or name.";
            return false;
        }
        if (!layer_ids.insert(layer.id).second) {
            error_message = "Scene \"" + scene.id + "\" contains duplicate layer id \""
                            + layer.id + "\".";
            return false;
        }
        if (!layer_names.insert(normalized_name(layer.name)).second) {
            error_message = "Scene \"" + scene.id + "\" contains duplicate layer name \""
                            + layer.name + "\".";
            return false;
        }
    }
    if (scene.defaultLayerId.empty() || !scene_has_layer(scene, scene.defaultLayerId)) {
        error_message = "Scene \"" + scene.id + "\" has an invalid defaultLayerId.";
        return false;
    }
    EntityId cameraTargetId = INVALID_ENTITY;
    for (const SceneInstanceDef &instance : scene.instances) {
        if (instance.layerId.empty() || !scene_has_layer(scene, instance.layerId)) {
            error_message = "Scene \"" + scene.id + "\" contains an instance with an invalid layerId.";
            return false;
        }
        if (instance.cameraTarget) {
            if (cameraTargetId != INVALID_ENTITY) {
                error_message = "Scene \"" + scene.id
                    + "\" contains more than one CameraTarget.";
                return false;
            }
            cameraTargetId = instance.id;
        }
    }
    for (const auto &[layer_id, settings] : scene.layerSettings) {
        (void)settings;
        if (!scene_has_layer(scene, layer_id)) {
            error_message = "Scene \"" + scene.id + "\" contains layer settings for unknown layer \""
                            + layer_id + "\".";
            return false;
        }
    }
    for (const auto &[layer_id, tilemap] : scene.tilemapLayers) {
        (void)tilemap;
        if (!scene_has_layer(scene, layer_id)) {
            error_message = "Scene \"" + scene.id + "\" contains tilemap data for unknown layer \""
                            + layer_id + "\".";
            return false;
        }
    }
    return true;
}

bool reject_legacy_key(const nlohmann::json &object,
                       const char *key,
                       std::string &error_message)
{
    if (!object.contains(key)) {
        return false;
    }
    error_message = std::string("Unsupported legacy field \"") + key + "\".";
    return true;
}

} // namespace

bool validate_current_project_document(const ProjectDoc &document, std::string &error_message)
{
    if (document.formatVersion != kCurrentProjectFormatVersion) {
        error_message = "Project formatVersion must be " + std::to_string(kCurrentProjectFormatVersion) + ".";
        return false;
    }
    if (document.scenes.empty() || document.activeSceneId.empty()
        || document.scenes.find(document.activeSceneId) == document.scenes.end()) {
        error_message = "Project must declare an activeSceneId that exists in scenes.";
        return false;
    }
    if (!validate_current_global_variables_document(document.globalVariables, error_message)) {
        return false;
    }
    for (const auto &[scene_id, scene] : document.scenes) {
        if (scene_id != scene.id) {
            error_message = "Scene map key and scene id must match.";
            return false;
        }
        if (!validate_scene_document(scene, error_message)) {
            return false;
        }
    }
    return true;
}

bool validate_current_project_json(const nlohmann::json &root, std::string &error_message)
{
    if (!root.is_object()) {
        error_message = "Project JSON root must be an object.";
        return false;
    }
    if (reject_legacy_key(root, "projectFormatVersion", error_message)
        || reject_legacy_key(root, "format_version", error_message)
        || reject_legacy_key(root, "layers", error_message)) {
        return false;
    }
    if (!root.contains("formatVersion") || !root["formatVersion"].is_number_integer()
        || root["formatVersion"].get<int>() != kCurrentProjectFormatVersion) {
        error_message = "Unsupported project format. Expected formatVersion "
                        + std::to_string(kCurrentProjectFormatVersion) + ".";
        return false;
    }
    if (!root.contains("activeSceneId") || !root["activeSceneId"].is_string()
        || root["activeSceneId"].get<std::string>().empty()) {
        error_message = "Project requires a non-empty activeSceneId.";
        return false;
    }
    if (!root.contains("scenes") || !root["scenes"].is_object() || root["scenes"].empty()) {
        error_message = "Project requires a non-empty scenes object.";
        return false;
    }
    if (!root.contains("globalVariables")
        || !validate_current_global_variables_json(root["globalVariables"], error_message)) {
        return false;
    }

    ProjectDoc document;
    document.formatVersion = kCurrentProjectFormatVersion;
    document.activeSceneId = root["activeSceneId"].get<std::string>();
    for (const auto &[scene_id, raw_scene] : root["scenes"].items()) {
        if (!raw_scene.is_object()
            || reject_legacy_key(raw_scene, "default_layer_id", error_message)
            || reject_legacy_key(raw_scene, "layer_settings", error_message)
            || reject_legacy_key(raw_scene, "tilemap_layers", error_message)) {
            if (error_message.empty()) {
                error_message = "Each scene must be an object.";
            }
            return false;
        }
        if (!raw_scene.contains("id") || !raw_scene["id"].is_string()
            || raw_scene["id"].get<std::string>() != scene_id) {
            error_message = "Each scene id must match its scenes map key.";
            return false;
        }
        if (!raw_scene.contains("layers") || !raw_scene["layers"].is_array()
            || raw_scene["layers"].empty()) {
            error_message = "Scene \"" + scene_id + "\" requires a non-empty layers array.";
            return false;
        }
        if (!raw_scene.contains("defaultLayerId") || !raw_scene["defaultLayerId"].is_string()) {
            error_message = "Scene \"" + scene_id + "\" requires defaultLayerId.";
            return false;
        }

        SceneDef scene;
        scene.id = scene_id;
        scene.defaultLayerId = raw_scene["defaultLayerId"].get<std::string>();
        for (const auto &raw_layer : raw_scene["layers"]) {
            if (!raw_layer.is_object() || !raw_layer.contains("id") || !raw_layer["id"].is_string()
                || !raw_layer.contains("name") || !raw_layer["name"].is_string()) {
                error_message = "Scene \"" + scene_id + "\" has an invalid layer record.";
                return false;
            }
            scene.layers.push_back({raw_layer["id"].get<std::string>(),
                                    raw_layer["name"].get<std::string>(),
                                    raw_layer.value("locked", false)});
        }
        if (raw_scene.contains("instances")) {
            if (!raw_scene["instances"].is_array()) {
                error_message = "Scene \"" + scene_id + "\" instances must be an array.";
                return false;
            }
            bool has_camera_target = false;
            for (const auto &raw_instance : raw_scene["instances"]) {
                if (!raw_instance.is_object() || !raw_instance.contains("layerId")
                    || !raw_instance["layerId"].is_string()) {
                    error_message = "Scene \"" + scene_id + "\" has an instance without layerId.";
                    return false;
                }
                if (raw_instance.contains("cameraTarget")) {
                    if (!raw_instance["cameraTarget"].is_object()) {
                        error_message = "Scene \"" + scene_id
                            + "\" has an invalid CameraTarget.";
                        return false;
                    }
                    if (has_camera_target) {
                        error_message = "Scene \"" + scene_id
                            + "\" contains more than one CameraTarget.";
                        return false;
                    }
                    has_camera_target = true;
                }
                SceneInstanceDef instance;
                instance.layerId = raw_instance["layerId"].get<std::string>();
                scene.instances.push_back(std::move(instance));
            }
        }
        for (const char *key : {"layerSettings", "tilemapLayers"}) {
            if (!raw_scene.contains(key)) {
                continue;
            }
            if (!raw_scene[key].is_object()) {
                error_message = "Scene \"" + scene_id + "\" field \"" + key + "\" must be an object.";
                return false;
            }
            for (const auto &[layer_id, value] : raw_scene[key].items()) {
                if (!value.is_object()) {
                    error_message = "Scene \"" + scene_id + "\" has invalid \"" + key + "\" data.";
                    return false;
                }
                if (std::string(key) == "layerSettings") {
                    scene.layerSettings.emplace(layer_id, SceneLayerSettings{});
                } else {
                    scene.tilemapLayers.emplace(layer_id, TilemapData{});
                }
            }
        }
        document.scenes.emplace(scene_id, std::move(scene));
    }
    return validate_current_project_document(document, error_message);
}

} // namespace ArtCade::ProjectJson
