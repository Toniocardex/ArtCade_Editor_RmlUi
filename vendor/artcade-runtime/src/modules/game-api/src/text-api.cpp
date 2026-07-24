#include "../include/game-api.h"
#include "../../renderer/include/renderer.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

namespace {

Vec4 parseTextColor(const std::string& color) {
    if      (color == "red")     return {1.f, 0.2f, 0.2f, 1.f};
    else if (color == "green")   return {0.2f, 1.f, 0.2f, 1.f};
    else if (color == "blue")    return {0.2f, 0.5f, 1.f, 1.f};
    else if (color == "white")   return {1.f, 1.f, 1.f, 1.f};
    else if (color == "black")   return {0.f, 0.f, 0.f, 1.f};
    else if (color == "yellow")  return {1.f, 1.f, 0.f, 1.f};
    else if (color == "cyan")    return {0.f, 1.f, 1.f, 1.f};
    else if (color == "magenta") return {1.f, 0.f, 1.f, 1.f};
    return {1.f, 1.f, 1.f, 1.f};
}

} // namespace

void GameAPI::bindTextAPI(sol::state& lua) {
    auto* renderer = ctx_.renderer;

    lua.set_function("text_draw",
        [renderer](const std::string& fontPath,
                   const std::string& text,
                   float x, float y,
                   sol::optional<int> size,
                   sol::optional<std::string> color) {
            const int fontSize = size.value_or(20);
            const Vec4 c = parseTextColor(color.value_or("white"));
            renderer->drawText(text, x, y, fontSize, c, fontPath);
        });

    lua.script(R"(
        text = text or {}
        -- fontPath: project-relative path (e.g. assets/fonts/ui.ttf); empty uses default font
        text.draw = function(fontPath, str, x, y, size, color)
            return text_draw(fontPath or "", str or "", x or 0, y or 0, size, color)
        end
    )");
}

} // namespace ArtCade::Modules
