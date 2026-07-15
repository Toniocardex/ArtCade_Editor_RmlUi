#include "editor-native/model/script_editor_state.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr std::size_t kMaxScriptUndoEntries = 256;

void pushBounded(std::vector<ScriptTextSnapshot>& history,
                 ScriptTextSnapshot snapshot) {
    if (history.size() == kMaxScriptUndoEntries) history.erase(history.begin());
    history.push_back(std::move(snapshot));
}
} // namespace

bool ScriptEditorBuffer::edit(std::string nextText, std::size_t nextCursorOffset) {
    nextCursorOffset = clampScriptCursorOffset(nextText, nextCursorOffset);
    if (text == nextText) {
        cursorOffset = nextCursorOffset;
        return false;
    }
    pushBounded(undoHistory, ScriptTextSnapshot{text, cursorOffset, revision});
    redoHistory.clear();
    text = std::move(nextText);
    cursorOffset = nextCursorOffset;
    revision = ++revisionHighWater;
    invalidateDiagnostics();
    return true;
}

bool ScriptEditorBuffer::undo() {
    if (undoHistory.empty()) return false;
    pushBounded(redoHistory, ScriptTextSnapshot{text, cursorOffset, revision});
    ScriptTextSnapshot previous = std::move(undoHistory.back());
    undoHistory.pop_back();
    text = std::move(previous.text);
    cursorOffset = clampScriptCursorOffset(text, previous.cursorOffset);
    revision = previous.revision;
    invalidateDiagnostics();
    return true;
}

bool ScriptEditorBuffer::redo() {
    if (redoHistory.empty()) return false;
    pushBounded(undoHistory, ScriptTextSnapshot{text, cursorOffset, revision});
    ScriptTextSnapshot next = std::move(redoHistory.back());
    redoHistory.pop_back();
    text = std::move(next.text);
    cursorOffset = clampScriptCursorOffset(text, next.cursorOffset);
    revision = next.revision;
    revisionHighWater = std::max(revisionHighWater, revision);
    invalidateDiagnostics();
    return true;
}

void ScriptEditorBuffer::markSaved(std::string persistedText) {
    if (text != persistedText) {
        text = std::move(persistedText);
        cursorOffset = clampScriptCursorOffset(text, cursorOffset);
        revision = ++revisionHighWater;
        invalidateDiagnostics();
    }
    savedText = text;
    savedRevision = revision;
}

void ScriptEditorBuffer::invalidateDiagnostics() {
    diagnostics.clear();
    validatedRevision.reset();
    validationPending = true;
}

ScriptEditorBuffer* ScriptEditorState::find(const AssetId& assetId) {
    const auto it = std::find_if(buffers.begin(), buffers.end(),
        [&](const ScriptEditorBuffer& buffer) { return buffer.scriptAssetId == assetId; });
    return it == buffers.end() ? nullptr : &*it;
}

const ScriptEditorBuffer* ScriptEditorState::find(const AssetId& assetId) const {
    const auto it = std::find_if(buffers.begin(), buffers.end(),
        [&](const ScriptEditorBuffer& buffer) { return buffer.scriptAssetId == assetId; });
    return it == buffers.end() ? nullptr : &*it;
}

ScriptEditorBuffer* ScriptEditorState::active() {
    return activeAssetId ? find(*activeAssetId) : nullptr;
}

const ScriptEditorBuffer* ScriptEditorState::active() const {
    return activeAssetId ? find(*activeAssetId) : nullptr;
}

bool ScriptEditorState::anyDirty() const {
    return std::any_of(buffers.begin(), buffers.end(),
        [](const ScriptEditorBuffer& buffer) { return buffer.dirty(); });
}

bool ScriptEditorState::open(AssetId assetId, std::string savedText) {
    if (ScriptEditorBuffer* existing = find(assetId)) {
        activeAssetId = existing->scriptAssetId;
        return false;
    }
    ScriptEditorBuffer buffer;
    buffer.scriptAssetId = std::move(assetId);
    buffer.text = std::move(savedText);
    buffer.savedText = buffer.text;
    activeAssetId = buffer.scriptAssetId;
    buffers.push_back(std::move(buffer));
    return true;
}

bool ScriptEditorState::close(const AssetId& assetId) {
    const auto it = std::find_if(buffers.begin(), buffers.end(),
        [&](const ScriptEditorBuffer& buffer) { return buffer.scriptAssetId == assetId; });
    if (it == buffers.end()) return false;
    const std::size_t index = static_cast<std::size_t>(it - buffers.begin());
    buffers.erase(it);
    if (activeAssetId == assetId) {
        if (buffers.empty()) activeAssetId.reset();
        else activeAssetId = buffers[std::min(index, buffers.size() - 1)].scriptAssetId;
    }
    return true;
}

void ScriptEditorState::clear() {
    buffers.clear();
    activeAssetId.reset();
    search.clear();
    editorFocused = false;
}

ScriptCursorPosition scriptCursorPosition(const std::string& text,
                                          std::size_t cursorOffset) {
    cursorOffset = clampScriptCursorOffset(text, cursorOffset);
    ScriptCursorPosition position;
    for (std::size_t i = 0; i < cursorOffset; ++i) {
        if (text[i] == '\n') {
            ++position.line;
            position.column = 1;
        } else if ((static_cast<unsigned char>(text[i]) & 0xc0u) != 0x80u) {
            ++position.column;
        }
    }
    return position;
}

std::size_t clampScriptCursorOffset(const std::string& text,
                                    std::size_t cursorOffset) {
    cursorOffset = std::min(cursorOffset, text.size());
    while (cursorOffset > 0 && cursorOffset < text.size()
           && (static_cast<unsigned char>(text[cursorOffset]) & 0xc0u) == 0x80u) {
        --cursorOffset;
    }
    return cursorOffset;
}

} // namespace ArtCade::EditorNative
