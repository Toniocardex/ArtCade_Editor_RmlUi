#pragma once

#include "core/types.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace ArtCade::EditorNative {

// =============================================================================
// EditorUiState — small, non-persistent UI layout/filter state (prompt §14).
//
// This is the only state panels are allowed to own beyond pure transient
// visuals. It holds NO authoring data (no scenes, entities, components). Panel
// widths, filters, expanded rows, open sections — nothing here belongs in the
// ProjectDocument.
// =============================================================================

namespace PanelLimits {
constexpr float kLeftMin    = 180.0f, kLeftMax    = 480.0f;
constexpr float kRightMin   = 220.0f, kRightMax   = 520.0f;
constexpr float kConsoleMin = 80.0f,  kConsoleMax = 600.0f;
} // namespace PanelLimits

struct EditorUiState {
    float leftPanelWidth  = 280.0f;
    float rightPanelWidth = 320.0f;
    float consoleHeight   = 220.0f;

    std::string hierarchyFilter;
    std::string assetFilter;

    bool consoleVisible = true;
    std::unordered_set<EntityId> expandedHierarchyItems;

    bool transformSectionExpanded = true;
    bool spriteSectionExpanded    = true;
    bool collisionSectionExpanded = false;
};

inline float clampLeftPanel(float v)    { return std::clamp(v, PanelLimits::kLeftMin,    PanelLimits::kLeftMax); }
inline float clampRightPanel(float v)   { return std::clamp(v, PanelLimits::kRightMin,   PanelLimits::kRightMax); }
inline float clampConsole(float v)      { return std::clamp(v, PanelLimits::kConsoleMin, PanelLimits::kConsoleMax); }

} // namespace ArtCade::EditorNative
