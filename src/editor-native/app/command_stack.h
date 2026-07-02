#pragma once

#include "editor-native/commands/editor_command.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

// One recorded authoring command plus the document revisions it moved between.
// Undo restores `revisionBefore`, redo restores `revisionAfter`, so dirty state
// is correct across an undo/redo walk (it never just allocates a new revision).
struct CommandEntry {
    std::unique_ptr<EditorCommand> command;
    uint64_t revisionBefore = 0;
    uint64_t revisionAfter = 0;
};

// =============================================================================
// CommandStack — undo + redo storage owned by the coordinator. A new command
// records onto the undo stack and discards the redo branch; undo/redo move a
// single entry between the two stacks. No transaction manager, no grouping.
// =============================================================================
class CommandStack {
public:
    void record(std::unique_ptr<EditorCommand> command,
                uint64_t revisionBefore, uint64_t revisionAfter) {
        redo_.clear();   // a fresh command makes the redo branch unreachable
        undo_.push_back(CommandEntry{std::move(command), revisionBefore, revisionAfter});
    }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    CommandEntry takeUndo() { return takeBack(undo_); }
    CommandEntry takeRedo() { return takeBack(redo_); }
    void pushUndo(CommandEntry entry) { undo_.push_back(std::move(entry)); }
    void pushRedo(CommandEntry entry) { redo_.push_back(std::move(entry)); }

    void clear() { undo_.clear(); redo_.clear(); }
    std::size_t size() const { return undo_.size(); }
    std::size_t redoSize() const { return redo_.size(); }

private:
    static CommandEntry takeBack(std::vector<CommandEntry>& stack) {
        CommandEntry entry = std::move(stack.back());
        stack.pop_back();
        return entry;
    }

    std::vector<CommandEntry> undo_;
    std::vector<CommandEntry> redo_;
};

} // namespace ArtCade::EditorNative
