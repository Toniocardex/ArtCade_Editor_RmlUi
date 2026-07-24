#include "../include/game-api.h"
#include "../../../core/runtime-profiler.h"
#include "../../renderer/include/renderer.h"
#include "../../editor-api/include/editor-api.h"

#include <sol/sol.hpp>
#include <iostream>

namespace ArtCade::Modules {

void GameAPI::bindDebugAPI(sol::state& lua) {
    auto* renderer = ctx_.renderer;
    auto* profiler = ctx_.profiler;

    lua.set_function("debug_log", [](const std::string& msg) {
        std::cout << "[Lua] " << msg << std::endl;  // endl flushes, ensuring capture in redirected stdout
        EditorAPI::queueConsoleLine(("[Lua] " + msg).c_str(), "lua");
    });

    lua.set_function("debug_drawLine",
        [renderer](float x1, float y1, float x2, float y2, const std::string& color) {
            // Simple color parsing: "red", "green", "blue", "#RRGGBB"
            Vec4 c = {1.f, 1.f, 1.f, 1.f};
            if      (color == "red")   c = {1.f, 0.f, 0.f, 1.f};
            else if (color == "green") c = {0.f, 1.f, 0.f, 1.f};
            else if (color == "blue")  c = {0.f, 0.f, 1.f, 1.f};
            else if (color == "white") c = {1.f, 1.f, 1.f, 1.f};
            else if (color == "black") c = {0.f, 0.f, 0.f, 1.f};
            renderer->drawLine(x1, y1, x2, y2, c);
        });

    lua.set_function("debug_drawRect",
        [renderer](float x, float y, float w, float h, const std::string& color) {
            Vec4 c = {1.f, 1.f, 0.f, 0.7f};
            if      (color == "red")     c = {1.f, 0.2f, 0.2f, 0.85f};
            else if (color == "green")   c = {0.2f, 1.f, 0.2f, 0.85f};
            else if (color == "blue")    c = {0.2f, 0.5f, 1.f, 0.85f};
            else if (color == "white")   c = {1.f,  1.f,  1.f, 0.85f};
            else if (color == "black")   c = {0.f,  0.f,  0.f, 0.85f};
            else if (color == "yellow")  c = {1.f,  1.f,  0.f, 0.7f};
            else if (color == "cyan")    c = {0.f,  1.f,  1.f, 0.7f};
            else if (color == "magenta") c = {1.f,  0.f,  1.f, 0.7f};
            else if (color == "orange")  c = {1.f,  0.6f, 0.f, 0.85f};
            renderer->drawRect(x, y, w, h, c);
        });

    // Shared colour parser (reused by drawCircle and drawText)
    auto parseColor = [](const std::string& color) -> Vec4 {
        if      (color == "red")     return {1.f, 0.2f, 0.2f, 0.9f};
        else if (color == "green")   return {0.2f, 1.f, 0.2f, 0.9f};
        else if (color == "blue")    return {0.2f, 0.5f, 1.f, 0.9f};
        else if (color == "white")   return {1.f,  1.f,  1.f, 1.f};
        else if (color == "black")   return {0.f,  0.f,  0.f, 1.f};
        else if (color == "yellow")  return {1.f,  1.f,  0.f, 0.9f};
        else if (color == "cyan")    return {0.f,  1.f,  1.f, 0.9f};
        else if (color == "magenta") return {1.f,  0.f,  1.f, 0.9f};
        else if (color == "orange")  return {1.f,  0.6f, 0.f, 0.9f};
        else if (color == "gray")    return {0.5f, 0.5f, 0.5f, 0.9f};
        else if (color == "purple")  return {0.6f, 0.f,  1.f, 0.9f};
        return {1.f, 1.f, 1.f, 1.f};  // default white
    };

    lua.set_function("debug_drawCircle",
        [renderer, parseColor](float x, float y, float r, const std::string& color) {
            renderer->drawCircle(x, y, r, parseColor(color));
        });

    lua.set_function("debug_drawText",
        [renderer, parseColor](const std::string& text, float x, float y,
                               int fontSize, const std::string& color) {
            renderer->drawText(text, x, y, fontSize, parseColor(color));
        });
    lua.set_function("debug_profile",
        [profiler](sol::this_state ts) -> sol::table {
            sol::state_view lua(ts);
            sol::table out = lua.create_table();
            if (!profiler) return out;
            const auto s = profiler->snapshot();
            out["luaMs"] = s.luaMs;
            out["physicsMs"] = s.physicsMs;
            out["gameplayMs"] = s.gameplayMs;
            out["renderMs"] = s.renderMs;
            out["entityCount"] = s.entityCount;
            out["activePhysicsBodies"] = s.activePhysicsBodies;
            out["luaEventCount"] = s.luaEventCount;
            out["luaTickEnabled"] = s.luaTickEnabled;
            return out;
        });

    lua.script(R"(
        debug = {}
        debug.log        = function(msg)                       return debug_log(msg)                              end
        debug.drawLine   = function(x1,y1,x2,y2,color)        return debug_drawLine(x1,y1,x2,y2,color or "white") end
        debug.drawRect   = function(x,y,w,h,color)            return debug_drawRect(x,y,w,h,color or "yellow")    end
        debug.drawCircle = function(x,y,r,color)              return debug_drawCircle(x,y,r,color or "white")     end
        debug.drawText   = function(text,x,y,fontSize,color)  return debug_drawText(text,x,y,fontSize or 20,color or "white") end
        debug.profile    = function()                         return debug_profile()                              end
    )");
}

} // namespace ArtCade::Modules
