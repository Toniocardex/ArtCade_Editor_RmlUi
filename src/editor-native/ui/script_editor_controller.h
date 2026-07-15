#pragma once

#include "core/types.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace Rml {
class ElementDocument;
class ElementFormControlTextArea;
}

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Narrow editing-surface boundary. The initial implementation is a static
// RmlUi textarea; a future native code editor can replace it without becoming
// a second buffer authority.
class ICodeEditorSurface {
public:
    virtual ~ICodeEditorSurface() = default;
    virtual void setText(const std::string& text, std::size_t cursorOffset,
                         float scrollTop) = 0;
    virtual std::string text() const = 0;
    virtual std::size_t cursorOffset() const = 0;
    virtual std::pair<std::size_t, std::size_t> selection() const = 0;
    virtual float scrollTop() const = 0;
    virtual void setSelection(std::size_t begin, std::size_t end) = 0;
    virtual void focus() = 0;
};

class ScriptEditorController {
public:
    ScriptEditorController(EditorCoordinator& coordinator,
                           Rml::ElementDocument* document);
    void detach();
    void refresh();

    void textChanged(const std::string& text);
    void cursorChanged();
    void setFocused(bool focused);
    void undo();
    void redo();
    void insertSpacesForTab();
    void findNext(const std::string& query);
    void goToLine(const std::string& lineText);

private:
    void refreshTabs();
    void refreshLineNumbers();
    void refreshStatus();
    void syncSurfaceFromActiveBuffer();

    EditorCoordinator& coordinator_;
    Rml::ElementDocument* document_ = nullptr;
    std::unique_ptr<ICodeEditorSurface> surface_;
    AssetId renderedAssetId_;
    std::uint64_t renderedRevision_ = 0;
};

} // namespace ArtCade::EditorNative
