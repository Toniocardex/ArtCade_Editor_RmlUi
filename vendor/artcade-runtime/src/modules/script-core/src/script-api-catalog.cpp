#include "script-api-catalog.h"

#include <algorithm>
#include <unordered_map>

namespace ArtCade::Scripts {
namespace {

constexpr ScriptApiParam kVersionParams[] = {
    {"version", "number", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kUpdateParams[] = {
    {"ctx", "table", ScriptApiUnit::None, false, nullptr},
    {"dt", "number", ScriptApiUnit::Seconds, false, nullptr},
};
constexpr ScriptApiParam kCtxOnlyParams[] = {
    {"ctx", "table", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kKeyParams[] = {
    {"ctx", "table", ScriptApiUnit::None, false, nullptr},
    {"key", "string", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kOtherParams[] = {
    {"ctx", "table", ScriptApiUnit::None, false, nullptr},
    {"other", "number", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kBoolParams[] = {
    {"visible", "boolean", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kXyParams[] = {
    {"x", "number", ScriptApiUnit::World, false, nullptr},
    {"y", "number", ScriptApiUnit::World, false, nullptr},
};
constexpr ScriptApiParam kDxDyParams[] = {
    {"dx", "number", ScriptApiUnit::World, false, nullptr},
    {"dy", "number", ScriptApiUnit::World, false, nullptr},
};
constexpr ScriptApiParam kRadiansParams[] = {
    {"radians", "number", ScriptApiUnit::Radians, false, nullptr},
};
constexpr ScriptApiParam kDeltaRadParams[] = {
    {"delta", "number", ScriptApiUnit::Radians, false, nullptr},
};
constexpr ScriptApiParam kScaleParams[] = {
    {"sx", "number", ScriptApiUnit::None, false, nullptr},
    {"sy", "number", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kAxisParams[] = {
    {"axis", "number", ScriptApiUnit::NormalizedAxis, false, nullptr},
};
constexpr ScriptApiParam kAnimPlayParams[] = {
    {"assetId", "string", ScriptApiUnit::None, false, nullptr},
    {"clipId", "string", ScriptApiUnit::None, false, nullptr},
};
constexpr ScriptApiParam kSpeedParams[] = {
    {"speed", "number", ScriptApiUnit::Multiplier, false, nullptr},
};
constexpr ScriptApiParam kAudioPlayParams[] = {
    {"assetId", "string", ScriptApiUnit::None, false, nullptr},
    {"volume", "number", ScriptApiUnit::Volume01, true, "1.0"},
};
constexpr ScriptApiParam kKeyNameParams[] = {
    {"name", "string", ScriptApiUnit::None, false, nullptr},
};

ScriptApiInsertKind defaultInsertKind(ScriptApiKind kind) {
    switch (kind) {
    case ScriptApiKind::Callback: return ScriptApiInsertKind::LifecycleCallback;
    case ScriptApiKind::GlobalFn: return ScriptApiInsertKind::ApiDeclaration;
    case ScriptApiKind::Method: return ScriptApiInsertKind::FunctionCall;
    case ScriptApiKind::CtxField: return ScriptApiInsertKind::Expression;
    }
    return ScriptApiInsertKind::None;
}

ScriptApiEntry entry(ScriptApiKind kind, const char* qualified, const char* parent,
                     const char* name, const char* signature, const char* insert,
                     const char* doc, const ScriptApiParam* params, std::size_t count,
                     const char* ret = "nil",
                     ScriptApiInsertKind insertKind = ScriptApiInsertKind::None,
                     bool explicitInsertKind = false) {
    ScriptApiEntry e;
    e.apiVersion = kScriptApiVersion;
    e.kind = kind;
    e.insertKind = explicitInsertKind ? insertKind : defaultInsertKind(kind);
    e.qualifiedName = qualified;
    e.parentPath = parent;
    e.name = name;
    e.signature = signature;
    e.insertText = insert;
    e.shortDoc = doc;
    e.params = params;
    e.paramCount = count;
    e.returnType = ret;
    return e;
}

const std::vector<ScriptApiEntry>& catalogStorage() {
    static const std::vector<ScriptApiEntry> kCatalog = {
        entry(ScriptApiKind::GlobalFn, "artcade.require_api_version", "artcade",
              "require_api_version", "require_api_version(version)",
              "artcade.require_api_version(1)",
              "Declares the Manual Script API version. Must be called at load; only 1 is accepted.",
              kVersionParams, 1),

        entry(ScriptApiKind::Callback, "on_start", "", "on_start",
              "on_start(ctx)", "on_start = function(ctx)\n    \nend",
              "Called once when the script scope starts after Play materialization.",
              kCtxOnlyParams, 1),
        entry(ScriptApiKind::Callback, "on_update", "", "on_update",
              "on_update(ctx, dt)", "on_update = function(ctx, dt)\n    \nend",
              "Called every frame after Logic Board update. dt is seconds.",
              kUpdateParams, 2),
        entry(ScriptApiKind::Callback, "on_key_pressed", "", "on_key_pressed",
              "on_key_pressed(ctx, key)", "on_key_pressed = function(ctx, key)\n    \nend",
              "Called when a tracked key is pressed this frame. key is a Logic key name.",
              kKeyParams, 2),
        entry(ScriptApiKind::Callback, "on_key_released", "", "on_key_released",
              "on_key_released(ctx, key)", "on_key_released = function(ctx, key)\n    \nend",
              "Called when a tracked key is released this frame.",
              kKeyParams, 2),
        entry(ScriptApiKind::Callback, "on_key_held", "", "on_key_held",
              "on_key_held(ctx, key)", "on_key_held = function(ctx, key)\n    \nend",
              "Called for each key held this frame.",
              kKeyParams, 2),
        entry(ScriptApiKind::Callback, "on_collision_enter", "", "on_collision_enter",
              "on_collision_enter(ctx, other)",
              "on_collision_enter = function(ctx, other)\n    \nend",
              "Called when a collision begins. other is the other entity id.",
              kOtherParams, 2),
        entry(ScriptApiKind::Callback, "on_collision_exit", "", "on_collision_exit",
              "on_collision_exit(ctx, other)",
              "on_collision_exit = function(ctx, other)\n    \nend",
              "Called when a collision ends. other is the other entity id.",
              kOtherParams, 2),

        entry(ScriptApiKind::CtxField, "ctx", "", "ctx", "ctx", "ctx",
              "Per-callback context table for the owning entity.", nullptr, 0, "table",
              ScriptApiInsertKind::None, true),
        entry(ScriptApiKind::CtxField, "ctx.entity_id", "ctx", "entity_id", "entity_id",
              "ctx.entity_id", "Owning entity id (integer).", nullptr, 0, "number"),
        entry(ScriptApiKind::CtxField, "ctx.self", "ctx", "self", "self", "ctx.self",
              "Owner transform / visibility / destroy helpers.", nullptr, 0, "table"),
        entry(ScriptApiKind::CtxField, "ctx.platformer", "ctx", "platformer", "platformer",
              "ctx.platformer", "Platformer controller helpers (requires component).",
              nullptr, 0, "table"),
        entry(ScriptApiKind::CtxField, "ctx.animation", "ctx", "animation", "animation",
              "ctx.animation", "Sprite animation helpers.", nullptr, 0, "table"),
        entry(ScriptApiKind::CtxField, "ctx.audio", "ctx", "audio", "audio", "ctx.audio",
              "Audio playback helpers.", nullptr, 0, "table"),
        entry(ScriptApiKind::CtxField, "ctx.input", "ctx", "input", "input", "ctx.input",
              "Input snapshot for this callback.", nullptr, 0, "table"),
        entry(ScriptApiKind::CtxField, "ctx.event", "ctx", "event", "event", "ctx.event",
              "Event payload table (collision other, otherwise other is nil).",
              nullptr, 0, "table"),
        entry(ScriptApiKind::CtxField, "ctx.event.other", "ctx.event", "other", "other",
              "ctx.event.other", "Other entity id during collision callbacks, else nil.",
              nullptr, 0, "number"),

        entry(ScriptApiKind::Method, "ctx.self.set_visible", "ctx.self", "set_visible",
              "set_visible(visible)", "set_visible(",
              "Show or hide the owning entity.", kBoolParams, 1),
        entry(ScriptApiKind::Method, "ctx.self.set_flip_x", "ctx.self", "set_flip_x",
              "set_flip_x(flip)", "set_flip_x(",
              "Mirror the owning entity sprite horizontally. True faces Left "
              "(art assumed to face Right when false).", kBoolParams, 1),
        entry(ScriptApiKind::Method, "ctx.self.set_position", "ctx.self", "set_position",
              "set_position(x, y)", "set_position(",
              "Set world position.", kXyParams, 2),
        entry(ScriptApiKind::Method, "ctx.self.translate", "ctx.self", "translate",
              "translate(dx, dy)", "translate(",
              "Translate by a world-space delta.", kDxDyParams, 2),
        entry(ScriptApiKind::Method, "ctx.self.set_rotation", "ctx.self", "set_rotation",
              "set_rotation(radians)", "set_rotation(",
              "Set absolute rotation in radians (API v1).", kRadiansParams, 1),
        entry(ScriptApiKind::Method, "ctx.self.rotate_by", "ctx.self", "rotate_by",
              "rotate_by(delta)", "rotate_by(",
              "Add a relative rotation in radians (API v1).", kDeltaRadParams, 1),
        entry(ScriptApiKind::Method, "ctx.self.set_scale", "ctx.self", "set_scale",
              "set_scale(sx, sy)", "set_scale(",
              "Set scale. Both components must be finite and > 0.", kScaleParams, 2),
        entry(ScriptApiKind::Method, "ctx.self.destroy", "ctx.self", "destroy",
              "destroy()", "destroy()",
              "Request deferred destroy of the owning entity.", nullptr, 0),

        entry(ScriptApiKind::Method, "ctx.platformer.move", "ctx.platformer", "move",
              "move(axis)", "move(",
              "Set horizontal move intent. Finite number (Logic Board clamps to [-1,1]).",
              kAxisParams, 1),
        entry(ScriptApiKind::Method, "ctx.platformer.jump", "ctx.platformer", "jump",
              "jump()", "jump()", "Request a jump.", nullptr, 0),
        entry(ScriptApiKind::Method, "ctx.platformer.is_grounded", "ctx.platformer",
              "is_grounded", "is_grounded()", "is_grounded()",
              "True when the platformer reports grounded.", nullptr, 0, "boolean"),
        entry(ScriptApiKind::Method, "ctx.platformer.is_falling", "ctx.platformer",
              "is_falling", "is_falling()", "is_falling()",
              "True when platformer_state is Falling (ADR-0016).", nullptr, 0, "boolean"),
        entry(ScriptApiKind::Method, "ctx.platformer.state", "ctx.platformer",
              "state", "state()", "state()",
              "Stopped | Moving | Jumping | Falling (ADR-0016).", nullptr, 0, "string"),
        entry(ScriptApiKind::Method, "ctx.platformer.is_moving", "ctx.platformer",
              "is_moving", "is_moving()", "is_moving()",
              "True when platformer state is Moving.", nullptr, 0, "boolean"),

        entry(ScriptApiKind::Method, "ctx.animation.play", "ctx.animation", "play",
              "play(assetId, clipId)", "play(",
              "Play a sprite animation clip by stable AssetId and clip id.",
              kAnimPlayParams, 2),
        entry(ScriptApiKind::Method, "ctx.animation.stop", "ctx.animation", "stop",
              "stop()", "stop()", "Stop the current animation.", nullptr, 0),
        entry(ScriptApiKind::Method, "ctx.animation.set_speed", "ctx.animation", "set_speed",
              "set_speed(speed)", "set_speed(",
              "Set playback speed multiplier (finite, > 0).", kSpeedParams, 1),

        entry(ScriptApiKind::Method, "ctx.audio.play", "ctx.audio", "play",
              "play(assetId [, volume])", "play(",
              "Play an audio asset. volume defaults to 1.0 and must be in [0, 1].",
              kAudioPlayParams, 2),

        entry(ScriptApiKind::Method, "ctx.input.is_key_down", "ctx.input", "is_key_down",
              "is_key_down(name)", "is_key_down(",
              "True if name is held in this callback's input snapshot.",
              kKeyNameParams, 1, "boolean"),
        entry(ScriptApiKind::Method, "ctx.input.is_key_pressed", "ctx.input", "is_key_pressed",
              "is_key_pressed(name)", "is_key_pressed(",
              "True if name was pressed this frame.", kKeyNameParams, 1, "boolean"),
        entry(ScriptApiKind::Method, "ctx.input.is_key_released", "ctx.input", "is_key_released",
              "is_key_released(name)", "is_key_released(",
              "True if name was released this frame.", kKeyNameParams, 1, "boolean"),
    };
    return kCatalog;
}

} // namespace

const std::vector<ScriptApiEntry>& scriptApiCatalogV1() {
    return catalogStorage();
}

const ScriptApiEntry* findScriptApiByQualifiedName(const std::string& qualifiedName) {
    static const auto index = [] {
        std::unordered_map<std::string, const ScriptApiEntry*> map;
        for (const ScriptApiEntry& entry : catalogStorage())
            map.emplace(entry.qualifiedName, &entry);
        return map;
    }();
    const auto it = index.find(qualifiedName);
    return it == index.end() ? nullptr : it->second;
}

std::vector<std::string> scriptApiCatalogBindingPaths() {
    std::vector<std::string> paths;
    paths.reserve(catalogStorage().size());
    for (const ScriptApiEntry& entry : catalogStorage())
        paths.emplace_back(entry.qualifiedName);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

std::string scriptApiUnitLabel(ScriptApiUnit unit) {
    switch (unit) {
    case ScriptApiUnit::None: return {};
    case ScriptApiUnit::World: return "world units";
    case ScriptApiUnit::Radians: return "radians";
    case ScriptApiUnit::Seconds: return "seconds";
    case ScriptApiUnit::NormalizedAxis: return "axis";
    case ScriptApiUnit::Volume01: return "0..1";
    case ScriptApiUnit::Multiplier: return "multiplier";
    }
    return {};
}

std::string formatScriptApiSignatureHelp(const ScriptApiEntry& entry) {
    std::string help = entry.signature ? entry.signature : "";
    if (entry.paramCount == 0 || !entry.params) return help;
    help += "  —  ";
    for (std::size_t i = 0; i < entry.paramCount; ++i) {
        if (i) help += ", ";
        const ScriptApiParam& p = entry.params[i];
        if (p.optional) help += "[";
        help += p.name;
        help += ": ";
        help += p.luaType;
        const std::string unit = scriptApiUnitLabel(p.unit);
        if (!unit.empty()) {
            help += " (";
            help += unit;
            help += ")";
        }
        if (p.optional) {
            if (p.defaultLiteral) {
                help += " = ";
                help += p.defaultLiteral;
            }
            help += "]";
        }
    }
    return help;
}

std::string formatScriptApiHover(const ScriptApiEntry& entry) {
    std::string hover = entry.qualifiedName;
    hover += "\n";
    hover += formatScriptApiSignatureHelp(entry);
    if (entry.shortDoc && entry.shortDoc[0] != '\0') {
        hover += "\n";
        hover += entry.shortDoc;
    }
    if (entry.returnType && std::string(entry.returnType) != "nil") {
        hover += "\nreturns ";
        hover += entry.returnType;
    }
    return hover;
}

} // namespace ArtCade::Scripts
