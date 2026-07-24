#include "sprite-json.h"

#include "json-primitives.h"

namespace ArtCade::ProjectJson {

bool read_sprite_component(const nlohmann::json& entityJson,
                           SpriteComponent& out) {
    if (!entityJson.contains("sprite") || !entityJson["sprite"].is_object())
        return false;

    const auto& j = entityJson["sprite"];
    SpriteComponent s;
    s.spriteAssetId = j.value("spriteAssetId", std::string{});
    if (j.contains("tint"))
        s.tint = read_vec4(j["tint"]);
    if (j.contains("fillColor"))
        s.fillColor = read_vec3(j["fillColor"]);
    else
        s.fillColor = {s.tint.r, s.tint.g, s.tint.b};
    s.alpha = j.value("alpha", 1.f);
    s.pivotFromAsset = j.value("pivotFromAsset", true);
    if (j.contains("pivot"))
        s.pivot = read_vec2(j["pivot"]);
    s.renderOrder = j.value("renderOrder", 0);
    if (j.contains("defaultClip"))
        s.defaultClip = j["defaultClip"].get<std::string>();
    s.playClipOnSpawn = j.value("playClipOnSpawn", false);

    out = s;
    return true;
}

} // namespace ArtCade::ProjectJson
