#include "../include/game-api.h"
#include "../../sprite-animator/include/sprite-animator.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <algorithm>
#include <sol/sol.hpp>
#include <string>
#include <vector>

namespace ArtCade::Modules {

namespace {

constexpr char kAnimKeySeparator = '\x1f';

std::string animKey(const std::string& source, const std::string& clip) {
    return source + kAnimKeySeparator + clip;
}

std::vector<std::string> animDispatchKeys(EntityId entityId,
                                          const std::string& className,
                                          const std::string& clipName)
{
    const std::string idKey = std::to_string(entityId);
    const std::string any = "*";
    const std::string clip = clipName.empty() ? any : clipName;

    std::vector<std::string> keys;
    auto add = [&keys](std::string key) {
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(std::move(key));
    };

    add(animKey(idKey, clip));
    add(animKey(idKey, any));
    if (!className.empty()) {
        add(animKey(className, clip));
        add(animKey(className, any));
    }
    add(animKey(any, clip));
    add(animKey(any, any));
    return keys;
}

uint32_t dispatchAnimList(sol::state& lua,
                          sol::table list,
                          EntityId entityId,
                          const std::string& clipName,
                          int frameIdx)
{
    if (!list.valid()) return 0;
    uint32_t dispatched = 0;
    for (size_t i = 1; ; ++i) {
        sol::object slot = list[i];
        if (!slot.valid() || slot == sol::nil) break;
        sol::protected_function fn = slot.as<sol::protected_function>();
        if (!fn.valid()) continue;
        auto result = fn(entityId, clipName, frameIdx);
        ++dispatched;
        if (!result.valid()) {
            sol::error err = result;
            sol::protected_function debugLog = lua["debug"]["log"];
            if (debugLog.valid())
                debugLog(std::string("[animation handler error] ") + err.what());
        }
    }
    return dispatched;
}

uint32_t dispatchAnimHandlers(sol::state& lua,
                              sol::table bag,
                              const std::vector<std::string>& keys,
                              EntityId entityId,
                              const std::string& clipName,
                              int frameIdx)
{
    if (!bag.valid()) return 0;
    uint32_t dispatched = 0;
    for (const std::string& key : keys) {
        sol::object listObj = bag[key];
        if (!listObj.is<sol::table>()) continue;
        dispatched += dispatchAnimList(lua, listObj.as<sol::table>(), entityId, clipName, frameIdx);
    }
    return dispatched;
}

// Lua bag name for each non-finish playback event kind.
const char* animBagField(SpriteAnimator::AnimEventKind kind) {
    switch (kind) {
        case SpriteAnimator::AnimEventKind::Start:  return "_onStart";
        case SpriteAnimator::AnimEventKind::Frame:  return "_onFrame";
        case SpriteAnimator::AnimEventKind::Loop:   return "_onLoop";
        case SpriteAnimator::AnimEventKind::Change: return "_onChanged";
    }
    return nullptr;
}

// The non-finish kinds the animator can emit, in bit order.
constexpr SpriteAnimator::AnimEventKind kAnimEventKinds[] = {
    SpriteAnimator::AnimEventKind::Start,
    SpriteAnimator::AnimEventKind::Frame,
    SpriteAnimator::AnimEventKind::Loop,
    SpriteAnimator::AnimEventKind::Change,
};

// True if a bag table holds at least one live handler. Bags map
// key -> { fn, ... }; an unsubscribed key can linger with an empty list, so we
// probe the nested lists rather than mere key presence. Over-reporting is safe
// (it only re-enables emission), under-reporting would drop events.
bool animBagHasHandlers(const sol::table& animation, const char* field) {
    sol::object bagObj = animation[field];
    if (!bagObj.is<sol::table>()) return false;
    for (auto&& entry : bagObj.as<sol::table>()) {
        const sol::object& list = entry.second;
        if (list.is<sol::table>() && list.as<sol::table>().size() > 0)
            return true;
    }
    return false;
}

} // namespace

void GameAPI::bindAnimationAPI(sol::state& lua) {
    auto* anim = ctx_.spriteAnimator;

    lua.set_function("animation_play", [anim](EntityId id, const std::string& clip) {
        if (anim) anim->play(id, clip);
    });

    lua.set_function("animation_currentClip", [anim](EntityId id) -> std::string {
        return anim ? anim->currentClip(id) : std::string{};
    });

    lua.set_function("animation_isPlaying", [anim](EntityId id) -> bool {
        return anim ? anim->isPlaying(id) : false;
    });

    lua.set_function("animation_frameIndex", [anim](EntityId id) -> int {
        return anim ? anim->frameIndex(id) : -1;
    });

    lua.set_function("animation_pollFinished", [anim](sol::this_state ts) -> sol::table {
        sol::state_view L(ts);
        sol::table out = L.create_table();
        if (!anim) return out;
        const auto events = anim->pollFinished();
        int i = 1;
        for (const auto& ev : events) {
            sol::table row = L.create_table();
            row["entityId"] = ev.entityId;
            row["clip"]     = ev.clipName;
            out[i++] = row;
        }
        return out;
    });

    lua.script(R"(
        animation = animation or {}
        animation.play = function(id, clip) if id and clip ~= "" then return animation_play(id, clip) end end
        animation.currentClip = function(id) return animation_currentClip(id) end
        animation.isPlaying = function(id) return animation_isPlaying(id) end
        animation.frameIndex = function(id) return animation_frameIndex(id) end

        animation.pollFinished = function()
            -- Deprecated: prefer animation.onFinished(source, clipName, fn).
            return animation_pollFinished()
        end

        local function animKey(source, clipName)
            return tostring(source or "*") .. "\31" .. tostring(clipName or "*")
        end

        local function registerAnimHandler(bag, entityOrClass, clipName, fn)
            assert(type(fn) == "function",
                "animation: handler must be a function")
            local key = animKey(entityOrClass, clipName)
            bag[key] = bag[key] or {}
            table.insert(bag[key], fn)
        end

        -- Single source of truth for playback event kinds: maps each public
        -- registrant (animation.onX) to its handler bag. clearHandlers and the
        -- registrants below both derive from it, and the bag names must match
        -- animBagField() in animation-api.cpp.
        local _anim_event_kinds = {
            { register = "onFinished", bag = "_onFinished" },
            { register = "onStart",    bag = "_onStart" },
            { register = "onFrame",    bag = "_onFrame" },
            { register = "onLoop",     bag = "_onLoop" },
            { register = "onChanged",  bag = "_onChanged" },
        }

        function animation.clearHandlers()
            for _, kind in ipairs(_anim_event_kinds) do
                animation[kind.bag] = {}
            end
        end
        animation.clearHandlers()   -- create the bags

        for _, kind in ipairs(_anim_event_kinds) do
            animation[kind.register] = function(entityOrClass, clipName, fn)
                registerAnimHandler(animation[kind.bag], entityOrClass, clipName, fn)
            end
        end
    )");
}

uint32_t GameAPI::dispatchAnimationEvents() {
    if (!ctx_.spriteAnimator) return 0;
    const auto finished = ctx_.spriteAnimator->pollFinished();
    const auto events = ctx_.spriteAnimator->pollEvents();
    return dispatchAnimationEvents(finished, events);
}

uint32_t GameAPI::dispatchAnimationEvents(
    const std::vector<SpriteAnimator::FinishEvent>& finished,
    const std::vector<SpriteAnimator::AnimEvent>& events) {
    if (!luaState_ || !ctx_.spriteAnimator) return 0;

    sol::state& lua = *luaState_;
    sol::table animation = lua["animation"];
    if (!animation.valid()) return 0;

    // Refresh the animator's watched-kinds mask every frame, before the
    // early-out. This must run even when no events flowed: a narrowed mask
    // suppresses the very events that would otherwise reveal a newly
    // registered handler, so coupling the refresh to event flow would
    // deadlock a handler registered after the mask narrowed.
    uint32_t watched = 0;
    for (const SpriteAnimator::AnimEventKind kind : kAnimEventKinds) {
        if (animBagHasHandlers(animation, animBagField(kind)))
            watched |= SpriteAnimator::animEventBit(kind);
    }
    ctx_.spriteAnimator->setWatchedEventKinds(watched);

    if (finished.empty() && events.empty()) return 0;

    auto classOf = [this](EntityId id) -> std::string {
        return ctx_.entityGateway ? ctx_.entityGateway->className(id) : std::string{};
    };

    uint32_t dispatched = 0;

    sol::table finishedBag = animation["_onFinished"];
    for (const SpriteAnimator::FinishEvent& ev : finished) {
        const auto keys = animDispatchKeys(ev.entityId, classOf(ev.entityId), ev.clipName);
        dispatched += dispatchAnimHandlers(lua, finishedBag, keys, ev.entityId, ev.clipName, -1);
    }

    for (const SpriteAnimator::AnimEvent& ev : events) {
        const char* field = animBagField(ev.kind);
        if (!field) continue;
        sol::object bagObj = animation[field];
        if (!bagObj.is<sol::table>()) continue;
        const auto keys = animDispatchKeys(ev.entityId, classOf(ev.entityId), ev.clipName);
        dispatched += dispatchAnimHandlers(
            lua, bagObj.as<sol::table>(), keys, ev.entityId, ev.clipName, ev.frameIdx);
    }

    return dispatched;
}

} // namespace ArtCade::Modules
