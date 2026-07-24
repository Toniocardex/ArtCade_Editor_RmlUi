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

InstanceNameParts parseInstanceName(std::string_view name) {
    InstanceNameParts parts;
    parts.root = std::string(name);

    // Terminal: " (" + digits (no leading zero unless single 0 — but N>=2) + ")"
    if (name.size() < 4 || name.back() != ')')
        return parts;
    const auto open = name.rfind(" (");
    if (open == std::string_view::npos || open + 2 >= name.size() - 1)
        return parts;
    const std::string_view digits = name.substr(open + 2, name.size() - open - 3);
    if (digits.empty())
        return parts;
    if (digits.size() > 1 && digits[0] == '0')
        return parts; // Entity (01) is not canonical
    for (char c : digits) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return parts;
    }
    int value = 0;
    for (char c : digits)
        value = value * 10 + (c - '0');
    if (value < 2)
        return parts;

    parts.root = std::string(name.substr(0, open));
    parts.ordinal = value;
    return parts;
}

std::string makeUniqueInstanceName(const SceneDef& scene,
                                   std::string_view requestedName) {
    const InstanceNameParts parts = parseInstanceName(requestedName);
    const std::string& root = parts.root;

    const auto taken = [&](const std::string& candidate) {
        for (const SceneInstanceDef& inst : scene.instances) {
            if (inst.instanceName == candidate) return true;
        }
        return false;
    };

    if (!taken(root)) return root;
    for (int n = 2;; ++n) {
        const std::string candidate = root + " (" + std::to_string(n) + ")";
        if (!taken(candidate)) return candidate;
    }
}

bool hasInstanceOverrides(const SceneInstanceDef& instance) {
    if (instance.spritePresentationOverride.has_value()) return true;
    if (instance.spriteRendererOverride.has_value()) return true;
    if (instance.spriteAnimatorOverride.has_value()) return true;
    if (!instance.localVariableOverrides.empty()) return true;
    return false;
}

bool hierarchyInstanceMatches(const HierarchySearchFields& item,
                              std::string_view query) {
    if (query.empty()) return true;
    if (containsInsensitive(item.instanceName, query)) return true;
    if (containsInsensitive(item.objectTypeName, query)) return true;
    if (containsInsensitive(item.objectTypeId, query)) return true;
    if (containsInsensitive(item.layerName, query)) return true;
    if (containsInsensitive(std::to_string(item.entityId), query)) return true;
    return false;
}

} // namespace ArtCade::EditorNative
