#pragma once

#include "core/types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

enum class DiagnosticSeverity { Info, Warning, Error };

struct ScriptDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    AssetId scriptAssetId;
    std::string path;
    int line = 0;
    int column = 0;
    std::optional<EntityId> entityId;
    std::string callback;
    std::string message;

    bool operator==(const ScriptDiagnostic& other) const {
        return severity == other.severity && code == other.code
            && scriptAssetId == other.scriptAssetId && path == other.path
            && line == other.line && column == other.column
            && entityId == other.entityId && callback == other.callback
            && message == other.message;
    }
};

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
    std::vector<ScriptDiagnostic> diagnostics;
    std::optional<std::uint64_t> validatedRevision;
    bool validationPending = true;

    bool dirty() const { return revision != savedRevision; }
    bool canUndo() const { return !undoHistory.empty(); }
    bool canRedo() const { return !redoHistory.empty(); }

    bool edit(std::string nextText, std::size_t nextCursorOffset);
    bool undo();
    bool redo();
    void markSaved(std::string persistedText);
    void invalidateDiagnostics();
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
