#include "editor-native/app/shortcuts/keyboard_shortcut_projection.h"

#include "editor-native/app/shortcuts/editor_action_catalog.h"
#include "editor-native/app/shortcuts/shortcut_format.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {

std::string toLower(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool matchesSearch(const KeyboardShortcutListItem& item, const std::string& needle) {
    if (needle.empty()) return true;
    if (toLower(item.label).find(needle) != std::string::npos) return true;
    if (toLower(item.description).find(needle) != std::string::npos) return true;
    if (toLower(item.categoryLabel).find(needle) != std::string::npos) return true;
    for (const std::string& gesture : item.formattedGestures) {
        if (toLower(gesture).find(needle) != std::string::npos) return true;
    }
    return false;
}

KeyboardShortcutsFilter aggregateTabFor(EditorActionCategory category) {
    switch (category) {
    case EditorActionCategory::Scene:
    case EditorActionCategory::Tilemap:
        return KeyboardShortcutsFilter::Scene;
    case EditorActionCategory::LogicBoard:
        return KeyboardShortcutsFilter::LogicBoard;
    case EditorActionCategory::ScriptEditor:
        return KeyboardShortcutsFilter::ScriptEditor;
    case EditorActionCategory::File:
    case EditorActionCategory::History:
    case EditorActionCategory::Run:
    case EditorActionCategory::Workspace:
    case EditorActionCategory::View:
    case EditorActionCategory::Console:
    case EditorActionCategory::Help:
        return KeyboardShortcutsFilter::General;
    }
    return KeyboardShortcutsFilter::General;
}

int sortGroupRank(KeyboardShortcutsFilter tab) {
    switch (tab) {
    case KeyboardShortcutsFilter::General: return 0;
    case KeyboardShortcutsFilter::Scene: return 1;
    case KeyboardShortcutsFilter::LogicBoard: return 2;
    case KeyboardShortcutsFilter::ScriptEditor: return 3;
    case KeyboardShortcutsFilter::All: return 4;
    }
    return 9;
}

const char* sectionHeadingFor(KeyboardShortcutsFilter tab) {
    switch (tab) {
    case KeyboardShortcutsFilter::General: return "GENERAL";
    case KeyboardShortcutsFilter::Scene: return "SCENE";
    case KeyboardShortcutsFilter::LogicBoard: return "LOGIC BOARD";
    case KeyboardShortcutsFilter::ScriptEditor: return "SCRIPT EDITOR";
    case KeyboardShortcutsFilter::All: return "ALL";
    }
    return "";
}

} // namespace

EditorActionContext makeShortcutHelpEvaluationContext(const EditorActionContext& live) {
    EditorActionContext out = live;
    out.modalOpen = false;
    out.popupOpen = false;
    out.textEditing = false;
    out.exclusiveCapture = false;
    out.helpDialogOpen = false;
    switch (live.workspace) {
    case EditorWorkspaceKind::LogicBoard:
        out.focus = KeyboardFocusDomain::LogicBoard;
        break;
    case EditorWorkspaceKind::ScriptEditor:
        out.focus = KeyboardFocusDomain::ScriptAssetList;
        break;
    case EditorWorkspaceKind::Scene:
    default:
        out.focus = KeyboardFocusDomain::SceneViewport;
        break;
    }
    return out;
}

KeyboardShortcutsFilter defaultKeyboardShortcutsFilterFor(EditorWorkspaceKind workspace) {
    switch (workspace) {
    case EditorWorkspaceKind::LogicBoard:
        return KeyboardShortcutsFilter::LogicBoard;
    case EditorWorkspaceKind::ScriptEditor:
        return KeyboardShortcutsFilter::ScriptEditor;
    case EditorWorkspaceKind::Scene:
        return KeyboardShortcutsFilter::Scene;
    }
    return KeyboardShortcutsFilter::All;
}

std::vector<KeyboardShortcutListItem> buildKeyboardShortcutProjection(
    const KeyboardShortcutProjectionInput& input) {
    std::unordered_set<EditorActionId> bound;
    const auto* bindings = shortcutBindings();
    const std::size_t bindingCount = shortcutBindingCount();
    for (std::size_t i = 0; i < bindingCount; ++i)
        bound.insert(bindings[i].action);

    const std::string needle = toLower(input.search);
    std::vector<KeyboardShortcutListItem> items;
    const auto* descriptors = actionDescriptors();
    const std::size_t descriptorCount = actionDescriptorCount();
    for (std::size_t i = 0; i < descriptorCount; ++i) {
        const EditorActionDescriptor& desc = descriptors[i];
        if (!bound.count(desc.id)) continue;

        const KeyboardShortcutsFilter tab = aggregateTabFor(desc.category);
        if (input.filter != KeyboardShortcutsFilter::All && input.filter != tab)
            continue;

        KeyboardShortcutListItem item;
        item.action = desc.id;
        item.label = std::string(desc.label);
        item.description = std::string(desc.description);
        item.categoryLabel = sectionHeadingFor(tab);
        item.formattedGestures = formatAllShortcuts(desc.id);
        if (item.formattedGestures.empty()) continue;
        item.currentlyAvailable =
            resolveActionState(desc.id, input.availabilityContext).enabled;

        if (!matchesSearch(item, needle)) continue;
        items.push_back(std::move(item));
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const KeyboardShortcutListItem& a,
                        const KeyboardShortcutListItem& b) {
                         const auto* da = findActionDescriptor(a.action);
                         const auto* db = findActionDescriptor(b.action);
                         const auto ta = da ? aggregateTabFor(da->category)
                                            : KeyboardShortcutsFilter::All;
                         const auto tb = db ? aggregateTabFor(db->category)
                                            : KeyboardShortcutsFilter::All;
                         if (sortGroupRank(ta) != sortGroupRank(tb))
                             return sortGroupRank(ta) < sortGroupRank(tb);
                         return a.label < b.label;
                     });
    return items;
}

} // namespace ArtCade::EditorNative
