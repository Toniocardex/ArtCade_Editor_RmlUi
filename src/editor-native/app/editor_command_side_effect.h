#pragma once

#include <string>
#include <utility>
#include <filesystem>

namespace ArtCade::EditorNative {

struct EditorCommandSideEffectResult {
    bool ok = true;
    std::string error;

    static EditorCommandSideEffectResult success() { return {}; }
    static EditorCommandSideEffectResult failure(std::string message) {
        return {false, std::move(message)};
    }
};

// Abstract companion for an external derived artifact owned by one Command
// history entry. The Command remains domain-only; the coordinator merely
// preserves atomic ordering around apply/undo/redo.
class EditorCommandSideEffect {
public:
    virtual ~EditorCommandSideEffect() = default;

    virtual EditorCommandSideEffectResult rollbackInitial() = 0;
    virtual EditorCommandSideEffectResult prepareUndo() = 0;
    virtual EditorCommandSideEffectResult rollbackUndo() = 0;
    virtual void commitUndo() = 0;
    virtual EditorCommandSideEffectResult prepareRedo() = 0;
    virtual EditorCommandSideEffectResult rollbackRedo() = 0;
    virtual void commitRedo() = 0;
    // Save As first validates every retained entry without mutation, then
    // commits the root change for all entries. This two-phase boundary prevents
    // a partially rebased history when one copied artifact is unavailable.
    virtual EditorCommandSideEffectResult validateProjectRootRebase(
        const std::filesystem::path& previousRoot,
        const std::filesystem::path& nextRoot) const = 0;
    virtual void rebaseProjectRoot(
        const std::filesystem::path& previousRoot,
        const std::filesystem::path& nextRoot) = 0;
};

} // namespace ArtCade::EditorNative
