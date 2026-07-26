#include "core/text-component-format.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

using ArtCade::GameVariableValue;
using ArtCade::TextComponent;
using ArtCade::formatTextValue;
using ArtCade::resolveTextDisplay;

namespace {

bool expectEqual(const std::string& actual,
                 const std::string& expected,
                 const char* label) {
    if (actual == expected) return true;
    std::cerr << label << ": expected \"" << expected
              << "\", got \"" << actual << "\"\n";
    return false;
}

} // namespace

int main() {
    bool ok = true;
    ok &= expectEqual(formatTextValue(GameVariableValue{12.0}, "text", 0), "12", "number text");
    ok &= expectEqual(formatTextValue(GameVariableValue{true}, "text", 0), "true", "boolean text");
    ok &= expectEqual(formatTextValue(GameVariableValue{12.6}, "integer", 0), "13", "rounded integer");
    ok &= expectEqual(formatTextValue(GameVariableValue{7.0}, "padded", 3), "007", "padded integer");
    ok &= expectEqual(formatTextValue(GameVariableValue{65.0}, "time", 0), "1:05", "time");
    ok &= expectEqual(formatTextValue(GameVariableValue{42.0}, "percent", 0), "42%", "percent");
    ok &= expectEqual(formatTextValue(GameVariableValue{3.14159}, "decimals", 2), "3.14", "decimals");
    ok &= expectEqual(formatTextValue(GameVariableValue{std::string{"invalid"}}, "integer", 0), "0",
                      "invalid number");

    TextComponent component;
    component.text = "fallback";
    component.prefix = "[";
    component.suffix = "]";
    component.format = "integer";
    ok &= expectEqual(resolveTextDisplay(component, std::nullopt), "[fallback]",
                      "static applies prefix/suffix");
    ok &= expectEqual(resolveTextDisplay(component, GameVariableValue{7.0}), "[7]",
                      "bound formats value");
    component.bindKey = "score";
    ok &= expectEqual(resolveTextDisplay(component, std::nullopt), "[fallback]",
                      "missing bind uses fallback text with prefix/suffix");

    if (ok) std::cout << "text_value_formatter_test: all checks passed\n";
    return ok ? 0 : 1;
}
