#pragma once

#include "types.h"

#include <optional>
#include <string>
#include <string_view>

namespace ArtCade {

enum class TextBindingScope {
    Global,
    Local,
};

enum class TextValueFormat {
    Text,
    Integer,
    Padded,
    Time,
    Percent,
    Decimals,
};

enum class TextAnchor {
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    Center,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

std::optional<TextBindingScope> textBindingScopeFromString(std::string_view text);
const char* textBindingScopeToString(TextBindingScope scope);

std::optional<TextValueFormat> textValueFormatFromString(std::string_view text);
const char* textValueFormatToString(TextValueFormat format);

/** Legacy horizontal-only values map onto the top row of the 3×3 grid. */
std::optional<TextAnchor> textAnchorFromString(std::string_view text);
const char* textAnchorToString(TextAnchor anchor);

bool sameTextComponent(const TextComponent& lhs, const TextComponent& rhs);

/** Format a bound variable for TextComponent display. */
std::string formatTextValue(const GameVariableValue& value,
                            TextValueFormat format,
                            int digits);

/** Format using the persisted format token (runtime / fail-soft path). */
std::string formatTextValue(const GameVariableValue& value,
                            std::string_view format,
                            int digits);

/**
 * Resolve the final display string:
 *   bound  → prefix + format(value) + suffix
 *   else   → prefix + component.text + suffix  (format ignored)
 */
std::string resolveTextDisplay(const TextComponent& component,
                               const std::optional<GameVariableValue>& boundValue);

/** True when @p type is compatible with the Text format token / Gauge Number-only. */
bool isTextFormatCompatibleWithVariableType(TextValueFormat format,
                                            GameVariableDefinition::Type type);
bool isTextFormatCompatibleWithVariableType(std::string_view format,
                                            GameVariableDefinition::Type type);
bool isGaugeCompatibleWithVariableType(GameVariableDefinition::Type type);

/** Authoring validation for a TextComponent (alpha == 1, known tokens, digits). */
bool validateTextComponent(const TextComponent& component, std::string& error);
bool validateGaugeComponent(const GaugeComponent& component, std::string& error);

} // namespace ArtCade
