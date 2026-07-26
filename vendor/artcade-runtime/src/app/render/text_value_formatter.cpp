#include "text_value_formatter.h"

#include <cmath>
#include <string>

namespace ArtCade::AppRender {

namespace {

double valueToNumber(const Modules::VariableManager::Value& value) {
    if (const auto* number = std::get_if<double>(&value)) return *number;
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
    if (const auto* text = std::get_if<std::string>(&value)) {
        try {
            return std::stod(*text);
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

} // namespace

double variableToNumber(const Modules::VariableManager::Value& value) {
    return valueToNumber(value);
}

} // namespace ArtCade::AppRender
