#include "editor-native/app/instance_name_policy.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ArtCade::EditorNative {

namespace {

std::string lowerCopy(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool containsInsensitive(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    return lowerCopy(hay).find(lowerCopy(needle)) != std::string::npos;
}

} // namespace

bool hasInstanceOverrides(const SceneInstanceDef& instance) {
    return instance.spritePresentationOverride.has_value()
        || instance.spriteRendererOverride.has_value()
        || instance.spriteAnimatorOverride.has_value()
        || !instance.localVariableOverrides.empty();
}

bool hierarchyInstanceMatches(const HierarchySearchFields& item,
                              std::string_view query) {
    return query.empty()
        || containsInsensitive(item.displayName, query)
        || containsInsensitive(item.objectTypeName, query)
        || containsInsensitive(item.objectTypeId, query)
        || containsInsensitive(item.layerName, query)
        || containsInsensitive(std::to_string(item.entityId), query);
}

} // namespace ArtCade::EditorNative
