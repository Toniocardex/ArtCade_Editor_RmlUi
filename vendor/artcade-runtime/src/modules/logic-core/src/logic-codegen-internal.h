#pragma once

#include "../include/logic-core.h"

#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace ArtCade::Logic::CodegenInternal {

std::string escapeLua(const std::string& value);

/** Single condition expression for guards and predicate-as-event triggers. */
std::string emitConditionExpression(const LogicBlockDef& condition,
                                    std::set<std::string>& requiredFeatures);

/** Emits the condition guard using NOT > AND > OR precedence. */
bool emitConditionGuard(std::ostringstream& lua,
                        const std::vector<LogicConditionClause>& conditions,
                        std::set<std::string>& requiredFeatures);

/**
 * Boolean Lua expression for authored conditions (NOT > AND > OR), or empty
 * when @p conditions is empty.
 */
std::string emitConditionsExpression(const std::vector<LogicConditionClause>& conditions,
                                     std::set<std::string>& requiredFeatures);

} // namespace ArtCade::Logic::CodegenInternal
