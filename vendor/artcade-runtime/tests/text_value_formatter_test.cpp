#include "app/render/text_value_formatter.h"

#include <cstdint>
#include <iostream>
#include <string>

using ArtCade::AppRender::formatTextValue;
using ArtCade::Modules::VariableManager;

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
    ok &= expectEqual(formatTextValue(VariableManager::Value{12.0}, "text", 0), "12", "number text");
    ok &= expectEqual(formatTextValue(VariableManager::Value{true}, "text", 0), "true", "boolean text");
    ok &= expectEqual(formatTextValue(VariableManager::Value{12.6}, "integer", 0), "13", "rounded integer");
    ok &= expectEqual(formatTextValue(VariableManager::Value{7.0}, "padded", 3), "007", "padded integer");
    ok &= expectEqual(formatTextValue(VariableManager::Value{65.0}, "time", 0), "1:05", "time");
    ok &= expectEqual(formatTextValue(VariableManager::Value{42.0}, "percent", 0), "42%", "percent");
    ok &= expectEqual(formatTextValue(VariableManager::Value{3.14159}, "decimals", 2), "3.14", "decimals");
    ok &= expectEqual(formatTextValue(VariableManager::Value{std::string{"invalid"}}, "integer", 0), "0", "invalid number");

    if (ok) std::cout << "text_value_formatter_test: all checks passed\n";
    return ok ? 0 : 1;
}
