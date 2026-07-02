#pragma once

#include "editor-native/app/rml_system.h"

#include <RmlUi_Renderer_GL3.h>

#include <filesystem>
#include <string>

namespace Rml {
class Context;
class ElementDocument;
}

namespace ArtCade::EditorNative {

// =============================================================================
// RmlHost — owns the RmlUi runtime: interfaces, context, fonts, the loaded
// document, and the update/render/resize/shutdown lifecycle (prompt §16 B).
// No project logic lives here; it is pure presentation plumbing.
// =============================================================================
class RmlHost {
public:
    bool initialize(int width, int height, float dpRatio,
                    const std::filesystem::path& resourceRoot,
                    const std::string& documentPath);
    Rml::ElementDocument* loadDocument(const std::string& documentPath);
    void resize(int width, int height, float dpRatio);
    void update();
    void render();           // assumes a current raylib drawing pass
    void shutdown();         // idempotent

    Rml::Context*         context()  const { return context_; }
    Rml::ElementDocument* document() const { return document_; }

    void toggleDebugger();

private:
    RmlSystem             system_;
    RenderInterface_GL3   renderer_;
    std::filesystem::path resourceRoot_;
    Rml::Context*         context_     = nullptr;
    Rml::ElementDocument* document_    = nullptr;
    bool                  initialized_ = false;
    bool                  debugger_    = false;
};

} // namespace ArtCade::EditorNative
