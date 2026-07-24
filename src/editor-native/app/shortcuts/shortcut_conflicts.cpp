#include "editor-native/app/shortcuts/shortcut_conflicts.h"

namespace ArtCade::EditorNative {

namespace {

bool maskOverlapU8(std::uint8_t a, std::uint8_t b) { return (a & b) != 0; }
bool maskOverlapU16(std::uint16_t a, std::uint16_t b) { return (a & b) != 0; }

} // namespace

bool shortcutScopesOverlap(const ShortcutScope& a, const ShortcutScope& b) {
    if (!maskOverlapU8(static_cast<std::uint8_t>(a.workspaces),
                       static_cast<std::uint8_t>(b.workspaces)))
        return false;
    if (!maskOverlapU16(static_cast<std::uint16_t>(a.focusDomains),
                        static_cast<std::uint16_t>(b.focusDomains)))
        return false;
    if (!maskOverlapU8(static_cast<std::uint8_t>(a.overlays),
                       static_cast<std::uint8_t>(b.overlays)))
        return false;
    if (!maskOverlapU8(static_cast<std::uint8_t>(a.modes),
                       static_cast<std::uint8_t>(b.modes)))
        return false;
    if (a.requireTilemapContext != b.requireTilemapContext)
        return false; // mutually exclusive tilemap vs non-tilemap Scene F
    if (a.allowTextEditing != b.allowTextEditing && (a.allowTextEditing || b.allowTextEditing)) {
        // One allows text and one doesn't — still overlap only if both can fire
        // in the same textEditing state. If either requires !text (default),
        // they don't both match when textEditing is true.
    }
    return true;
}

bool validateShortcutCatalogConflicts(const ShortcutBinding* bindings,
                                      std::size_t count,
                                      ShortcutConflict* outConflict) {
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (!(bindings[i].gesture == bindings[j].gesture)) continue;
            if (!shortcutScopesOverlap(bindings[i].scope, bindings[j].scope)) continue;
            if (outConflict) {
                outConflict->gesture = bindings[i].gesture;
                outConflict->first = bindings[i].action;
                outConflict->second = bindings[j].action;
            }
            return false;
        }
    }
    return true;
}

} // namespace ArtCade::EditorNative
