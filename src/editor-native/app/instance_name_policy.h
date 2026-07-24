#pragma once

#include "core/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace ArtCade::EditorNative {

struct InstanceNameParts {
    std::string root;
    std::optional<int> ordinal; // set only for canonical " (N)" with N >= 2
};

/** ADR-0023: parse terminal " (N)" with N >= 2, no leading zeros. */
InstanceNameParts parseInstanceName(std::string_view name);

/**
 * Scene-unique instance display name. Uses canonical root + first free ordinal
 * from 2 when the root is taken. Case-sensitive, matches authoring equality.
 */
std::string makeUniqueInstanceName(const SceneDef& scene,
                                   std::string_view requestedName);

/** True when instance carries Object Type override fields (ADR-0023). */
bool hasInstanceOverrides(const SceneInstanceDef& instance);

/** Fields the Hierarchy filter matches against (ADR-0023). Presentation-only. */
struct HierarchySearchFields {
    std::string instanceName;
    std::string objectTypeName;
    std::string objectTypeId;
    std::string layerName;
    EntityId entityId = INVALID_ENTITY;
};

/** Case-insensitive match on name, type name/id, layer name, or entity id. */
bool hierarchyInstanceMatches(const HierarchySearchFields& item,
                              std::string_view query);

} // namespace ArtCade::EditorNative
