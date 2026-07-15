#pragma once

#include "core/types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

struct ScriptTextSnapshot {
    std::string   text;
    std::size_t   cursorOffset = 0;
    std::uint64_t revision = 0;
};

struct ScriptEditorBuffer {
    AssetId scriptAssetId;
    std::string text;
    std::string savedText;
    std::uint64_t revision = 0;
    std::uint64_t savedRevision = 0;
    std::uint64_t revisionHighWater = 0;
    std::size_t cursorOffset = 0;
    float scrollTop = 0.f;
    std::vector<ScriptTextSnapshot> undoHistory;
    std::vector<ScriptTextSnapshot> redoHistory;

    bool dirty() const { return revision != savedRevision; }
    bool canUndo() const { return !undoHistory.empty(); }
    bool canRedo() const { return !redoHistory.empty(); }

    bool edit(std::string nextText, std::size_t nextCursorOffset);
    bool undo();
    bool redo();
    void markSaved(std::string persistedText);
};

struct ScriptEditorState {
    std::vector<ScriptEditorBuffer> buffers;
    std::optional<AssetId> activeAssetId;
    std::string search;
    bool editorFocused = false;

    ScriptEditorBuffer* find(const AssetId& assetId);
    const ScriptEditorBuffer* find(const AssetId& assetId) const;
    ScriptEditorBuffer* active();
    const ScriptEditorBuffer* active() const;
    bool anyDirty() const;
    bool open(AssetId assetId, std::string savedText);
    bool close(const AssetId& assetId);
    void clear();
};

struct ScriptCursorPosition {
    std::size_t line = 1;
    std::size_t column = 1;
};

ScriptCursorPosition scriptCursorPosition(const std::string& text,
                                          std::size_t cursorOffset);
std::size_t clampScriptCursorOffset(const std::string& text,
                                    std::size_t cursorOffset);

} // namespace ArtCade::EditorNative
