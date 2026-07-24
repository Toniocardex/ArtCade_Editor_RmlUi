#pragma once

#include "../../modules/variable-manager/include/variable-manager.h"

#include <string>

namespace ArtCade::AppRender {

/** Format a runtime variable for a TextComponent binding. */
std::string formatTextValue(const Modules::VariableManager::Value& value,
                            const std::string& format,
                            int digits);

/** Coerce a runtime variable to a number (for GaugeComponent fill ratio). */
double variableToNumber(const Modules::VariableManager::Value& value);

} // namespace ArtCade::AppRender
