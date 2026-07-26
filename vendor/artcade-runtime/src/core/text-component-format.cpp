#include "text-component-format.h"

#include <cmath>
#include <cstdio>

namespace ArtCade {
namespace {

std::string valueToString(const GameVariableValue& value) {
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

double valueToNumber(const GameVariableValue& value) {
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

bool nearlyEqual(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

bool sameColor(const Vec4& a, const Vec4& b) {
    return nearlyEqual(a.r, b.r) && nearlyEqual(a.g, b.g)
        && nearlyEqual(a.b, b.b) && nearlyEqual(a.a, b.a);
}

} // namespace

std::optional<TextBindingScope> textBindingScopeFromString(std::string_view text) {
    if (text == "global") return TextBindingScope::Global;
    if (text == "local") return TextBindingScope::Local;
    return std::nullopt;
}

const char* textBindingScopeToString(TextBindingScope scope) {
    switch (scope) {
    case TextBindingScope::Global: return "global";
    case TextBindingScope::Local: return "local";
    }
    return "global";
}

std::optional<TextValueFormat> textValueFormatFromString(std::string_view text) {
    if (text.empty() || text == "text") return TextValueFormat::Text;
    if (text == "integer") return TextValueFormat::Integer;
    if (text == "padded") return TextValueFormat::Padded;
    if (text == "time") return TextValueFormat::Time;
    if (text == "percent") return TextValueFormat::Percent;
    if (text == "decimals") return TextValueFormat::Decimals;
    return std::nullopt;
}

const char* textValueFormatToString(TextValueFormat format) {
    switch (format) {
    case TextValueFormat::Text: return "text";
    case TextValueFormat::Integer: return "integer";
    case TextValueFormat::Padded: return "padded";
    case TextValueFormat::Time: return "time";
    case TextValueFormat::Percent: return "percent";
    case TextValueFormat::Decimals: return "decimals";
    }
    return "text";
}

std::optional<TextAnchor> textAnchorFromString(std::string_view text) {
    if (text == "top-left" || text == "left") return TextAnchor::TopLeft;
    if (text == "top-center" || text == "top") return TextAnchor::TopCenter;
    if (text == "top-right" || text == "right") return TextAnchor::TopRight;
    if (text == "middle-left") return TextAnchor::MiddleLeft;
    if (text == "center" || text == "middle-center") return TextAnchor::Center;
    if (text == "middle-right") return TextAnchor::MiddleRight;
    if (text == "bottom-left") return TextAnchor::BottomLeft;
    if (text == "bottom-center" || text == "bottom") return TextAnchor::BottomCenter;
    if (text == "bottom-right") return TextAnchor::BottomRight;
    return std::nullopt;
}

const char* textAnchorToString(TextAnchor anchor) {
    switch (anchor) {
    case TextAnchor::TopLeft: return "top-left";
    case TextAnchor::TopCenter: return "top-center";
    case TextAnchor::TopRight: return "top-right";
    case TextAnchor::MiddleLeft: return "middle-left";
    case TextAnchor::Center: return "center";
    case TextAnchor::MiddleRight: return "middle-right";
    case TextAnchor::BottomLeft: return "bottom-left";
    case TextAnchor::BottomCenter: return "bottom-center";
    case TextAnchor::BottomRight: return "bottom-right";
    }
    return "top-left";
}

bool sameTextComponent(const TextComponent& lhs, const TextComponent& rhs) {
    return lhs.text == rhs.text
        && lhs.bindKey == rhs.bindKey
        && lhs.bindScope == rhs.bindScope
        && lhs.format == rhs.format
        && lhs.digits == rhs.digits
        && lhs.prefix == rhs.prefix
        && lhs.suffix == rhs.suffix
        && lhs.fontPath == rhs.fontPath
        && lhs.size == rhs.size
        && sameColor(lhs.color, rhs.color)
        && lhs.align == rhs.align
        && nearlyEqual(lhs.offsetX, rhs.offsetX)
        && nearlyEqual(lhs.offsetY, rhs.offsetY)
        && lhs.screenSpace == rhs.screenSpace;
}

std::string formatTextValue(const GameVariableValue& value,
                            TextValueFormat format,
                            int digits) {
    if (format == TextValueFormat::Text) return valueToString(value);

    const double number = valueToNumber(value);
    char buffer[64];
    if (digits < 0) digits = 0;

    switch (format) {
    case TextValueFormat::Integer:
        std::snprintf(buffer, sizeof(buffer), "%lld", std::llround(number));
        break;
    case TextValueFormat::Padded:
        std::snprintf(buffer, sizeof(buffer), "%0*lld", digits, std::llround(number));
        break;
    case TextValueFormat::Time: {
        long long seconds = std::llround(number);
        if (seconds < 0) seconds = 0;
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", seconds / 60, seconds % 60);
        break;
    }
    case TextValueFormat::Percent:
        std::snprintf(buffer, sizeof(buffer), "%lld%%", std::llround(number));
        break;
    case TextValueFormat::Decimals:
        std::snprintf(buffer, sizeof(buffer), "%.*f", digits, number);
        break;
    case TextValueFormat::Text:
        return valueToString(value);
    }
    return buffer;
}

std::string formatTextValue(const GameVariableValue& value,
                            std::string_view format,
                            int digits) {
    const auto parsed = textValueFormatFromString(format);
    return formatTextValue(value, parsed.value_or(TextValueFormat::Text), digits);
}

std::string resolveTextDisplay(const TextComponent& component,
                               const std::optional<GameVariableValue>& boundValue) {
    const std::string value = boundValue
        ? formatTextValue(*boundValue, component.format, component.digits)
        : component.text;
    return component.prefix + value + component.suffix;
}

bool isTextFormatCompatibleWithVariableType(TextValueFormat format,
                                            GameVariableDefinition::Type type) {
    if (format == TextValueFormat::Text) {
        return type == GameVariableDefinition::Type::Number
            || type == GameVariableDefinition::Type::Boolean
            || type == GameVariableDefinition::Type::String;
    }
    return type == GameVariableDefinition::Type::Number;
}

bool isTextFormatCompatibleWithVariableType(std::string_view format,
                                            GameVariableDefinition::Type type) {
    const auto parsed = textValueFormatFromString(format);
    if (!parsed) return false;
    return isTextFormatCompatibleWithVariableType(*parsed, type);
}

bool isGaugeCompatibleWithVariableType(GameVariableDefinition::Type type) {
    return type == GameVariableDefinition::Type::Number;
}

bool validateTextComponent(const TextComponent& component, std::string& error) {
    if (!textValueFormatFromString(component.format)) {
        error = "Text format must be text|integer|padded|time|percent|decimals";
        return false;
    }
    if (!textBindingScopeFromString(component.bindScope)) {
        error = "Text bindScope must be global or local";
        return false;
    }
    if (!textAnchorFromString(component.align)) {
        error = "Text align must be a 3x3 anchor (or legacy left|center|right)";
        return false;
    }
    if (component.digits < 0) {
        error = "Text digits must be >= 0";
        return false;
    }
    if (component.size <= 0) {
        error = "Text size must be > 0";
        return false;
    }
    if (!(component.color.a >= 0.999f && component.color.a <= 1.001f)) {
        error = "Text authoring color alpha must be 1";
        return false;
    }
    if (!std::isfinite(component.color.r) || !std::isfinite(component.color.g)
        || !std::isfinite(component.color.b)
        || component.color.r < 0.f || component.color.r > 1.f
        || component.color.g < 0.f || component.color.g > 1.f
        || component.color.b < 0.f || component.color.b > 1.f) {
        error = "Text color channels must be finite in [0,1]";
        return false;
    }
    if (!std::isfinite(component.offsetX) || !std::isfinite(component.offsetY)) {
        error = "Text offsets must be finite";
        return false;
    }
    if (!component.bindKey.empty() && component.bindScope == "none") {
        error = "Text bindScope \"none\" is not persisted";
        return false;
    }
    return true;
}

bool validateGaugeComponent(const GaugeComponent& component, std::string& error) {
    if (!textBindingScopeFromString(component.bindScope)) {
        error = "Gauge bindScope must be global or local";
        return false;
    }
    if (component.direction != "horizontal" && component.direction != "vertical") {
        error = "Gauge direction must be horizontal or vertical";
        return false;
    }
    if (!(component.maxValue > 0.f) || !std::isfinite(component.maxValue)) {
        error = "Gauge maxValue must be finite and > 0";
        return false;
    }
    if (!(component.width > 0.f) || !(component.height > 0.f)
        || !std::isfinite(component.width) || !std::isfinite(component.height)) {
        error = "Gauge size must be finite and > 0";
        return false;
    }
    if (!(component.fillColor.a >= 0.999f && component.fillColor.a <= 1.001f)
        || !(component.bgColor.a >= 0.999f && component.bgColor.a <= 1.001f)) {
        error = "Gauge authoring color alpha must be 1";
        return false;
    }
    return true;
}

} // namespace ArtCade
