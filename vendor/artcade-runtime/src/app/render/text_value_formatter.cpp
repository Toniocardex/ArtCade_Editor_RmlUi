#include "text_value_formatter.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace ArtCade::AppRender {

namespace {

std::string valueToString(const Modules::VariableManager::Value& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    if (const auto* number = std::get_if<double>(&value)) {
        char buffer[32];
        if (std::isfinite(*number) && *number == std::floor(*number)) {
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(*number));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%g", *number);
        }
        return buffer;
    }
    return "";
}

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

std::string formatTextValue(const Modules::VariableManager::Value& value,
                            const std::string& format,
                            int digits) {
    if (format.empty() || format == "text") return valueToString(value);

    const double number = valueToNumber(value);
    char buffer[64];
    if (digits < 0) digits = 0;

    if (format == "integer") {
        std::snprintf(buffer, sizeof(buffer), "%lld", std::llround(number));
    } else if (format == "padded") {
        std::snprintf(buffer, sizeof(buffer), "%0*lld", digits, std::llround(number));
    } else if (format == "time") {
        long long seconds = std::llround(number);
        if (seconds < 0) seconds = 0;
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", seconds / 60, seconds % 60);
    } else if (format == "percent") {
        std::snprintf(buffer, sizeof(buffer), "%lld%%", std::llround(number));
    } else if (format == "decimals") {
        std::snprintf(buffer, sizeof(buffer), "%.*f", digits, number);
    } else {
        return valueToString(value);
    }
    return buffer;
}

} // namespace ArtCade::AppRender
