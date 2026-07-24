#include "entity-json.h"

#include "json-primitives.h"
#include "physics-json.h"
#include "sprite-json.h"
#include "../modules/logic-core/include/logic-core.h"

#include <unordered_set>

namespace ArtCade::ProjectJson {

namespace {

bool read_variable_value(const nlohmann::json& raw,
                         GameVariableDefinition::Type type,
                         GameVariableValue& out) {
    if (type == GameVariableDefinition::Type::Number && raw.is_number()) {
        out = raw.get<double>();
        return true;
    }
    if (type == GameVariableDefinition::Type::Boolean && raw.is_boolean()) {
        out = raw.get<bool>();
        return true;
    }
    if (type == GameVariableDefinition::Type::String && raw.is_string()) {
        out = raw.get<std::string>();
        return true;
    }
    return false;
}

/** "#rrggbb" → Vec4 (alpha 1); falls back to white on malformed input. */
Vec4 parse_hex_color(const std::string& hex) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) return {1.f, 1.f, 1.f, 1.f};
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = nibble(h[i]);
        if (v[i] < 0) return {1.f, 1.f, 1.f, 1.f};
    }
    return {
        static_cast<float>(v[0] * 16 + v[1]) / 255.f,
        static_cast<float>(v[2] * 16 + v[3]) / 255.f,
        static_cast<float>(v[4] * 16 + v[5]) / 255.f,
        1.f,
    };
}

void read_optional_gameplay_components(const nlohmann::json& j, EntityDef& e) {
    if (j.contains("platformerController") && j["platformerController"].is_object()) {
        const auto& p = j["platformerController"];
        PlatformerControllerComponent pc;
        pc.maxSpeed      = p.value("maxSpeed", 300.f);
        pc.jumpForce     = p.value("jumpForce", 600.f);
        pc.customGravity = p.value("customGravity", 1500.f);
        pc.coyoteTime    = p.value("coyoteTime", 0.15f);
        pc.jumpBuffer    = p.value("jumpBuffer", 0.1f);
        pc.climbSpeed    = p.value("climbSpeed", 120.f);
        e.platformerController = pc;
    }
    if (j.contains("topDownController") && j["topDownController"].is_object()) {
        const auto& t = j["topDownController"];
        TopDownControllerComponent tc;
        tc.maxSpeed       = t.value("maxSpeed", 260.f);
        tc.acceleration   = t.value("acceleration", 1600.f);
        tc.friction       = t.value("friction", 2200.f);
        tc.fourDirections = t.value("fourDirections", false);
        e.topDownController = tc;
    }
    if (j.contains("linearMover") && j["linearMover"].is_object()) {
        const auto& m = j["linearMover"];
        LinearMoverComponent lm;
        lm.directionX = m.value("directionX", 1.f);
        lm.directionY = m.value("directionY", 0.f);
        lm.speed      = m.value("speed", 300.f);
        e.linearMover = lm;
    }
    if (j.contains("cameraTarget") && j["cameraTarget"].is_object()) {
        const auto& c = j["cameraTarget"];
        CameraTargetComponent ct;
        ct.offsetX     = c.value("offsetX", 0.f);
        ct.offsetY     = c.value("offsetY", 0.f);
        ct.followSpeed = c.value("followSpeed", 8.f);
        e.cameraTarget = ct;
    }
    if (j.contains("magneticItem") && j["magneticItem"].is_object()) {
        const auto& m = j["magneticItem"];
        MagneticItemComponent mi;
        mi.attractTag = m.value("attractTag", std::string("pickup"));
        mi.radius     = m.value("radius", 200.f);
        mi.pullSpeed  = m.value("pullSpeed", 400.f);
        e.magneticItem = mi;
    }
    if (j.contains("hordeMember") && j["hordeMember"].is_object()) {
        const auto& h = j["hordeMember"];
        HordeMemberComponent hm;
        hm.targetClass      = h.value("targetClass", std::string("Player"));
        hm.maxSpeed         = h.value("maxSpeed", 120.f);
        hm.separationRadius = h.value("separationRadius", 48.f);
        hm.separationWeight = h.value("separationWeight", 1.5f);
        hm.chaseWeight      = h.value("chaseWeight", 1.f);
        e.hordeMember = hm;
    }
    if (j.contains("autoDestroy") && j["autoDestroy"].is_object()) {
        AutoDestroyComponent ac;
        ac.lifespan = j["autoDestroy"].value("lifespan", 0.f);
        e.autoDestroy = ac;
    }
    if (j.contains("dialog") && j["dialog"].is_object()) {
        const auto& d = j["dialog"];
        DialogComponent dc;
        dc.dialogId       = d.value("dialogId", "");
        dc.startNode      = d.value("startNode", "");
        dc.textSpeed      = d.value("textSpeed", 40.f);
        dc.triggerMessage = d.value("triggerMessage", "");
        if (!dc.dialogId.empty())
            e.dialog = dc;
    }
    if (j.contains("text") && j["text"].is_object()) {
        const auto& t = j["text"];
        TextComponent tc;
        tc.text        = t.value("text", "");
        tc.bindKey     = t.value("bindKey", "");
        tc.bindScope   = t.value("bindScope", std::string("global"));
        tc.format      = t.value("format", std::string("text"));
        tc.digits      = t.value("digits", 2);
        tc.prefix      = t.value("prefix", "");
        tc.suffix      = t.value("suffix", "");
        tc.fontPath    = t.value("fontPath", "");
        tc.size        = t.value("size", 24);
        tc.color       = parse_hex_color(t.value("colorHex", std::string("#ffffff")));
        tc.align       = t.value("align", std::string("top-left"));
        tc.offsetX     = t.value("offsetX", 0.f);
        tc.offsetY     = t.value("offsetY", 0.f);
        tc.screenSpace = t.value("screenSpace", false);
        e.text = tc;
    }
    if (j.contains("gauge") && j["gauge"].is_object()) {
        const auto& g = j["gauge"];
        GaugeComponent gc;
        gc.bindKey     = g.value("bindKey", "");
        gc.bindScope   = g.value("bindScope", std::string("global"));
        gc.maxValue    = g.value("maxValue", 100.f);
        gc.width       = g.value("width", 64.f);
        gc.height      = g.value("height", 8.f);
        gc.fillColor   = parse_hex_color(g.value("fillColorHex", std::string("#3ad13a")));
        gc.bgColor     = parse_hex_color(g.value("bgColorHex", std::string("#202020")));
        gc.direction   = g.value("direction", std::string("horizontal"));
        gc.offsetX     = g.value("offsetX", 0.f);
        gc.offsetY     = g.value("offsetY", -40.f);
        gc.screenSpace = g.value("screenSpace", false);
        e.gauge = gc;
    }
    if (j.contains("boxCollider2D") && j["boxCollider2D"].is_object()) {
        const auto& c = j["boxCollider2D"];
        BoxCollider2DComponent bc;
        bc.offset = read_vec2(c.value("offset", nlohmann::json::object()), bc.offset);
        bc.size = read_vec2(c.value("size", nlohmann::json::object()), bc.size);
        bc.enabled = c.value("enabled", bc.enabled);
        const std::string mode = c.value("mode", std::string{});
        if (mode == "trigger") bc.mode = BoxColliderMode::Trigger;
        else if (mode == "oneWayPlatform") bc.mode = BoxColliderMode::OneWayPlatform;
        else if (mode == "solid") bc.mode = BoxColliderMode::Solid;
        else if (c.value("isTrigger", false)) bc.mode = BoxColliderMode::Trigger;
        e.boxCollider2D = bc;
    }
    if (j.contains("visible") && j["visible"].is_boolean())
        e.visible = j["visible"].get<bool>();
}

} // namespace

void read_variable_definitions(const nlohmann::json& raw,
                               std::vector<GameVariableDefinition>& out) {
    out.clear();
    if (!raw.is_array()) return;
    std::unordered_set<std::string> seen;
    for (const auto& item : raw) {
        if (!item.is_object()) continue;
        GameVariableDefinition def;
        def.key = item.value("key", std::string{});
        const std::string type = item.value("type", std::string{});
        if (def.key.empty() || !seen.insert(def.key).second) continue;
        if (type == "number") def.type = GameVariableDefinition::Type::Number;
        else if (type == "boolean") def.type = GameVariableDefinition::Type::Boolean;
        else if (type == "string") def.type = GameVariableDefinition::Type::String;
        else continue;
        if (!item.contains("initialValue")
            || !read_variable_value(item["initialValue"], def.type, def.initialValue)) continue;
        def.description = item.value("description", std::string{});
        out.push_back(std::move(def));
    }
}

void read_entity_components(const nlohmann::json& entityJson, EntityDef& out) {
    if (entityJson.contains("transform") && entityJson["transform"].is_object())
        out.transform = read_transform(entityJson["transform"]);
    read_sprite_component(entityJson, out.sprite);
    if (entityJson.contains("spritePresentation")
        && entityJson["spritePresentation"].is_object()) {
        const auto& value = entityJson["spritePresentation"];
        SpritePresentationComponent presentation;
        presentation.visible = value.value("visible", true);
        if (value.contains("source") && value["source"].is_object()) {
            const auto& source = value["source"];
            const std::string kind = source.value("kind", std::string{"none"});
            if (kind == "image") {
                presentation.source = SpritePresentationImage{
                    source.value("assetId", source.value("imageAssetId", std::string{}))};
            } else if (kind == "animation") {
                presentation.source = SpritePresentationAnimation{
                    source.value("assetId", source.value("animationAssetId", std::string{})),
                    source.value("defaultClipId", source.value("initialClipId", std::string{})),
                    source.value("autoPlay", true), source.value("playbackSpeed", 1.f)};
            }
        }
        out.spritePresentation = std::move(presentation);
    }
    if (entityJson.contains("spriteRenderer")
        && entityJson["spriteRenderer"].is_object()) {
        const auto& value = entityJson["spriteRenderer"];
        SpriteRendererComponent renderer;
        renderer.imageAssetId = value.value("imageAssetId", std::string{});
        renderer.visible = value.value("visible", true);
        out.spriteRenderer = std::move(renderer);
    }
    if (entityJson.contains("spriteAnimator")
        && entityJson["spriteAnimator"].is_object()) {
        const auto& value = entityJson["spriteAnimator"];
        SpriteAnimatorComponent animator;
        animator.animationAssetId = value.value("animationAssetId", std::string{});
        animator.defaultClipId = value.value(
            "defaultClipId", value.value("initialClipId", std::string{}));
        animator.autoPlay = value.value("autoPlay", true);
        animator.playbackSpeed = value.value("playbackSpeed", 1.f);
        // Legacy fold: animation lived on the renderer.
        if (animator.animationAssetId.empty()
            && entityJson.contains("spriteRenderer")
            && entityJson["spriteRenderer"].is_object()) {
            animator.animationAssetId = entityJson["spriteRenderer"].value(
                "animationAssetId", std::string{});
        }
        out.spriteAnimator = std::move(animator);
    }
    if (entityJson.contains("scripts") && entityJson["scripts"].is_object()) {
        const auto& value = entityJson["scripts"];
        ScriptComponent scripts;
        if (value.contains("attachments") && value["attachments"].is_array()) {
            for (const auto& item : value["attachments"]) {
                if (!item.is_object()) continue;
                ScriptAttachmentDef attachment;
                attachment.id = item.value("id", std::string{});
                attachment.scriptAssetId = item.value("scriptAssetId", std::string{});
                attachment.enabled = item.value("enabled", true);
                scripts.attachments.push_back(std::move(attachment));
            }
        }
        out.scripts = std::move(scripts);
    }
    read_physics_component(entityJson, out.physics);
    // ADR-0014: "collisionBody" on objectTypes/entities is no longer current
    // authoring. Ignore the key (technical debt — no silent migration of
    // multi-shape / circle / layer profiles into BoxCollider2D). Tile palette
    // still reads collisionBody via project-meta-json.
    read_optional_gameplay_components(entityJson, out);
    if (entityJson.contains("localVariables"))
        read_variable_definitions(entityJson["localVariables"], out.localVariables);
    if (entityJson.contains("localVariableOverrides")
        && entityJson["localVariableOverrides"].is_object()) {
        out.localVariableOverrides.clear();
        for (const auto& def : out.localVariables) {
            if (!entityJson["localVariableOverrides"].contains(def.key)) continue;
            GameVariableValue value;
            if (read_variable_value(entityJson["localVariableOverrides"][def.key], def.type, value))
                out.localVariableOverrides[def.key] = std::move(value);
        }
    }
}

void read_entity_instance(const nlohmann::json& entityJson,
                          EntityId fallbackId,
                          EntityDef& out,
                          bool use_entity_name_fallback) {
    out.id = entityJson.value("id", fallbackId);
    if (use_entity_name_fallback) {
        out.name = entityJson.value(
            "name", std::string("Entity_") + std::to_string(fallbackId));
    } else {
        out.name = entityJson.value("name", std::string{});
    }
    out.className = entityJson.value("className", std::string{});
    out.layerId = entityJson.value("layerId", std::string{});
    if (entityJson.contains("tags") && entityJson["tags"].is_array())
        out.tags = entityJson["tags"].get<std::vector<std::string>>();
    read_entity_components(entityJson, out);
}

void read_object_type(const nlohmann::json& typeJson,
                      const std::string& mapKey,
                      EntityDef& out) {
    out.id = 0;
    out.className = typeJson.value("id", mapKey);
    out.name = typeJson.value("displayName", out.className);
    if (typeJson.contains("tags") && typeJson["tags"].is_array())
        out.tags = typeJson["tags"].get<std::vector<std::string>>();
    read_entity_components(typeJson, out);
}

void read_entities_map(const nlohmann::json& doc,
                       std::unordered_map<EntityId, EntityDef>& out,
                       bool use_entity_name_fallback) {
    out.clear();
    if (!doc.contains("entities"))
        return;

    const auto& ents = doc["entities"];
    if (ents.is_array()) {
        for (const auto& item : ents) {
            EntityDef entity;
            read_entity_instance(
                item, static_cast<EntityId>(out.size() + 1), entity, use_entity_name_fallback);
            if (entity.id != 0)
                out[entity.id] = std::move(entity);
        }
    } else if (ents.is_object()) {
        for (auto& [key, val] : ents.items()) {
            const EntityId fallbackId = static_cast<EntityId>(std::stoul(key));
            EntityDef entity;
            read_entity_instance(val, fallbackId, entity, use_entity_name_fallback);
            if (entity.id != 0)
                out[entity.id] = std::move(entity);
        }
    }
}

void read_object_types_map(const nlohmann::json& doc,
                            std::unordered_map<std::string, EntityDef>& out) {
    out.clear();

    const nlohmann::json* raw = nullptr;
    if (doc.contains("objectTypes")
        && (doc["objectTypes"].is_object() || doc["objectTypes"].is_array()))
        raw = &doc["objectTypes"];
    if (raw == nullptr)
        return;

    if (raw->is_array()) {
        for (const auto& val : *raw) {
            if (!val.is_object()) continue;
            const std::string key = val.value("id", std::string{});
            EntityDef entity;
            read_object_type(val, key, entity);
            if (!entity.className.empty()) out[entity.className] = std::move(entity);
        }
    } else {
        for (auto& [key, val] : raw->items()) {
            if (!val.is_object()) continue;
            EntityDef entity;
            read_object_type(val, key, entity);
            if (!entity.className.empty()) out[entity.className] = std::move(entity);
        }
    }
}

bool read_object_type_logic_boards(const nlohmann::json& doc,
                                   std::unordered_map<std::string, EntityDef>& objectTypes,
                                   std::string* error) {
    const nlohmann::json* rawTypes = nullptr;
    if (doc.contains("objectTypes")
        && (doc["objectTypes"].is_object() || doc["objectTypes"].is_array()))
        rawTypes = &doc["objectTypes"];
    if (rawTypes == nullptr) return true;

    const auto readBoard = [&](const std::string& mapKey, const nlohmann::json& rawType) -> bool {
        if (!rawType.is_object() || !rawType.contains("logicBoard")) return true;
        const ObjectTypeId typeId = rawType.value("id", mapKey);
        const auto typeIt = objectTypes.find(typeId);
        if (typeIt == objectTypes.end()) {
            if (error) *error = "logicBoard references unknown object type: " + typeId;
            return false;
        }
        LogicBoardDef board;
        const Logic::LogicJsonResult parsed = Logic::logicBoardFromJson(rawType["logicBoard"], board);
        if (!parsed.ok) {
            if (error) *error = "logicBoard for '" + typeId + "': " + parsed.error;
            return false;
        }
        const auto diagnostics = Logic::validateBoard(typeId, board, &typeIt->second);
        if (!diagnostics.empty()) {
            if (error) *error = "logicBoard for '" + typeId + "': " + diagnostics.front().message;
            return false;
        }
        typeIt->second.logicBoard = std::move(board);
        return true;
    };

    if (rawTypes->is_array()) {
        for (const auto& rawType : *rawTypes)
            if (!readBoard({}, rawType)) return false;
    } else {
        for (auto& [key, rawType] : rawTypes->items())
            if (!readBoard(key, rawType)) return false;
    }
    return true;
}

} // namespace ArtCade::ProjectJson
