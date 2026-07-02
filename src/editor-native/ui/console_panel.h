#pragma once

#include <cstddef>
#include <optional>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Appends the coordinator's console messages. Refreshed only on a Console
// invalidation (prompt §11).
//
// Row selection is purely local panel state (prompt §3: "selezione visuale") —
// no Command, Intent, EditorState or ProjectDocument. It exists only to let the
// user copy one full message to the clipboard. The selected index is into the
// coordinator's full console log; it is clamped/reset on every refresh, so a log
// that shrinks (or a replaced project) can never leave a dangling selection.
class ConsolePanel {
public:
    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator);

    // Click selection. Ignores an out-of-range index; repaints rows + Copy button.
    void select(std::size_t messageIndex, Rml::ElementDocument* document,
                const EditorCoordinator& coordinator);

    std::optional<std::size_t> selectedIndex() const { return selected_; }

private:
    void updateCopyButton(Rml::ElementDocument* document) const;

    std::optional<std::size_t> selected_;
};

} // namespace ArtCade::EditorNative
