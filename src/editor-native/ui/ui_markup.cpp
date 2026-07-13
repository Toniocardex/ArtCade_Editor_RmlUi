#include "editor-native/ui/ui_markup.h"

#include "editor-native/ui/editor_ui.h"   // escapeRml

namespace ArtCade::EditorNative {

std::string iconMarkup(const char* codepoint) {
    return std::string("<span class=\"icon\">") + codepoint + "</span>";
}

std::string dropdownTriggerMarkup(const std::string& valueText,
                                  const std::string& toggleAction,
                                  const std::string& dropdownId,
                                  bool open, bool disabled,
                                  const std::string& extraClass) {
    std::string row = "<div class=\"drop-trigger";
    if (!extraClass.empty()) row += " " + extraClass;
    if (open) row += " open";
    if (disabled) row += " disabled";
    row += "\"";
    if (!disabled) {
        row += " data-action=\"" + toggleAction + "\" data-arg=\"" + dropdownId + "\"";
    }
    row += ">" + escapeRml(valueText)
         + "<span class=\"drop-caret\">&#xeb5d;</span></div>";
    return row;
}

} // namespace ArtCade::EditorNative
