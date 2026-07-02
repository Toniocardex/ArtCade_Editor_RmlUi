#include "editor-native/app/rml_host.h"

#include "editor-native/ui/editor_fonts.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <raylib.h>

namespace ArtCade::EditorNative {

bool RmlHost::initialize(int width, int height, float dpRatio,
                         const std::filesystem::path& resourceRoot,
                         const std::string& documentPath) {
    if (initialized_) return true;

    Rml::SetSystemInterface(&system_);
    Rml::SetRenderInterface(&renderer_);
    if (!Rml::Initialise()) return false;
    if (!renderer_) {
        Rml::Shutdown();
        return false;
    }
    resourceRoot_ = resourceRoot;

    const FontLoadResult fontResult = loadEditorFonts(resourceRoot);
    if (!fontResult.ok) {
        TraceLog(LOG_ERROR, "[editor] %s", fontResult.error.c_str());
        Rml::Shutdown();
        return false;
    }

    context_ = Rml::CreateContext("editor", Rml::Vector2i(width, height));
    if (!context_) {
        Rml::Shutdown();
        return false;
    }
    context_->SetDensityIndependentPixelRatio(dpRatio);
    renderer_.SetViewport(width, height);
    Rml::Debugger::Initialise(context_);

    const std::filesystem::path fullDocumentPath = resourceRoot / documentPath;
    document_ = context_->LoadDocument(fullDocumentPath.string());
    if (document_) document_->Show();

    initialized_ = true;
    return document_ != nullptr;
}

Rml::ElementDocument* RmlHost::loadDocument(const std::string& documentPath) {
    if (!context_) return nullptr;
    const std::filesystem::path fullDocumentPath = resourceRoot_ / documentPath;
    return context_->LoadDocument(fullDocumentPath.string());
}

void RmlHost::resize(int width, int height, float dpRatio) {
    if (!context_) return;
    context_->SetDimensions(Rml::Vector2i(width, height));
    context_->SetDensityIndependentPixelRatio(dpRatio);
    renderer_.SetViewport(width, height);
}

void RmlHost::update() {
    if (context_) context_->Update();
}

void RmlHost::render() {
    if (!context_) return;
    renderer_.BeginFrame();
    context_->Render();
    renderer_.EndFrame();
}

void RmlHost::toggleDebugger() {
    if (!context_) return;
    debugger_ = !debugger_;
    Rml::Debugger::SetVisible(debugger_);
}

void RmlHost::shutdown() {
    if (!initialized_) return;
    Rml::Shutdown();   // releases contexts, documents and textures (via ReleaseTexture)
    context_ = nullptr;
    document_ = nullptr;
    initialized_ = false;
}

} // namespace ArtCade::EditorNative
