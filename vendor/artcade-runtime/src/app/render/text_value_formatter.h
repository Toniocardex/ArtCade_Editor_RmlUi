#pragma once

#include "../../modules/variable-manager/include/variable-manager.h"
#include "../../core/text-component-format.h"

#include <string>

namespace ArtCade::AppRender {

/** Format a runtime variable for a TextComponent binding. */
inline std::string formatTextValue(const Modules::VariableManager::Value& value,
                                   const std::string& format,
                                   int digits) {
    return ArtCade::formatTextValue(value, format, digits);
}

/** Coerce a runtime variable to a number (for GaugeComponent fill ratio). */
double variableToNumber(const Modules::VariableManager::Value& value);

} // namespace ArtCade::AppRender
