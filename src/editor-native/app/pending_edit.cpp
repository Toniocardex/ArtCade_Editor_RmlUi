#include "editor-native/app/pending_edit.h"

#include "editor-native/app/inspector_commit.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string_view>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

bool isNumericCommit(const std::string& action) {
    static constexpr std::string_view actions[] = {
        "commit-pos-x", "commit-pos-y", "commit-animator-speed",
        "commit-box-offset-x", "commit-box-offset-y",
        "commit-box-size-x", "commit-box-size-y",
        "commit-mover-dir-x", "commit-mover-dir-y", "commit-mover-speed",
        "commit-topdown-speed", "commit-platformer-move",
        "commit-platformer-jump", "commit-platformer-gravity",
        "commit-scene-width", "commit-scene-height",
        "commit-tilemap-cell-width", "commit-tilemap-cell-height",
        "commit-grid-cell-size", "commit-animation-clip-fps",
        "commit-animation-columns", "commit-animation-rows",
        "commit-animation-margin", "commit-animation-spacing",
        "commit-tileset-tile-width", "commit-tileset-tile-height",
        "commit-tileset-margin-x", "commit-tileset-margin-y",
        "commit-tileset-spacing-x", "commit-tileset-spacing-y",
        "commit-logic-position-x", "commit-logic-position-y",
        "commit-logic-animation-speed",
        "commit-sfx-field",
    };
    return std::find(std::begin(actions), std::end(actions), action) != std::end(actions);
}

bool requiresNonEmptyText(const std::string& action) {
    static constexpr std::string_view actions[] = {
        "commit-project-name", "commit-scene-name", "commit-name",
        "commit-type-name", "commit-layer-rename",
        "commit-animation-clip-name", "commit-tileset-name",
        "commit-sfx-name",
    };
    return std::find(std::begin(actions), std::end(actions), action) != std::end(actions);
}

bool allWhitespace(const std::string& value) {
    return !value.empty()
        && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isspace(c) != 0;
           });
}

bool incompleteNumericBuffer(const std::string& value) {
    if (value.empty() || allWhitespace(value)) return true;
    if (value == "+" || value == "-" || value == "."
        || value == "+." || value == "-.") {
        return true;
    }

    // A trailing decimal separator is a live edit only when the prefix is
    // already a complete finite number. This keeps "abc." invalid.
    if (value.back() == '.' && value.size() > 1) {
        return parseNumberField(value.substr(0, value.size() - 1)).has_value();
    }

    const std::size_t exponent = value.find_last_of("eE");
    if (exponent == std::string::npos) return false;
    const bool missingExponent = exponent + 1 == value.size();
    const bool exponentSignOnly = exponent + 2 == value.size()
        && (value.back() == '+' || value.back() == '-');
    if (!missingExponent && !exponentSignOnly) return false;
    return exponent > 0 && parseNumberField(value.substr(0, exponent)).has_value();
}

PendingEditResult failure(PendingEditStatus status, std::string message) {
    return PendingEditResult{status, std::move(message)};
}

} // namespace

PendingEditResult classifyPendingEdit(const std::string& action,
                                      const std::string& value) {
    if (action.rfind("commit-", 0) != 0) return {};

    if (isNumericCommit(action)) {
        if (incompleteNumericBuffer(value)) {
            return failure(PendingEditStatus::Incomplete,
                           "Finish the focused numeric value before continuing");
        }
        if (!parseNumberField(value).has_value()) {
            return failure(PendingEditStatus::Invalid,
                           "The focused field contains an invalid numeric value");
        }
        return {};
    }

    if (requiresNonEmptyText(action) && value.empty()) {
        return failure(PendingEditStatus::Invalid,
                       "The focused name cannot be empty");
    }
    return {};
}

bool pendingEditNeedsCommit(const std::string& action,
                            const std::string& value,
                            const std::string& renderedValue) {
    return action == "commit-layer-rename" || value != renderedValue;
}

} // namespace ArtCade::EditorNative
