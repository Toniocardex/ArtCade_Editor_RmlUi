#pragma once

#include "../../../core/logic-number-expression.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ArtCade::Logic {

/** Serialize one NumberExpression (compact number for literals). */
nlohmann::json numberExpressionToJson(const NumberExpression& expression);

/**
 * Parse a number or expression object into @p out.
 * @param allowStructured when false (board schema 3), only JSON numbers succeed.
 */
bool numberExpressionFromJson(const nlohmann::json& json, NumberExpression& out,
                              std::string& error, bool allowStructured = true);

} // namespace ArtCade::Logic
