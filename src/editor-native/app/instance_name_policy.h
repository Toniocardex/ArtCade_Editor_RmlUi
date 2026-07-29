#pragma once

#include "core/types.h"

#include <string>
#include <string_view>

namespace ArtCade::EditorNative {

/** True when instance carries Object Type override fields (ADR-0023). */
bool hasInstanceOverrides(const SceneInstanceDef& instance);

/** Fields the Hierarchy filter matches against (ADR-0023). Presentation-only. */
struct HierarchySearchFields {
    std::string displayName;
    std::string objectTypeName;
    std::string objectTypeId;
    std::string layerName;
    EntityId entityId = INVALID_ENTITY;
};

/** Case-insensitive match on name, type name/id, layer name, or entity id. */
bool hierarchyInstanceMatches(const HierarchySearchFields& item,
                              std::string_view query);

} // namespace ArtCade::EditorNative
