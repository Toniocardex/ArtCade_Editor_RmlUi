#include "editor-native/app/rml_host.h"

#include "editor-native/ui/editor_fonts.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <raylib.h>

namespace ArtCade::EditorNative {

RmlHost::~RmlHost() { shutdown(); }

bool RmlHost::initialize(int width, int height, float dpRatio,
                         const std::filesystem::path& resourceRoot,
                         const std::string& documentPath) {
    if (initialized_) return true;

    Rml::SetSystemInterface(&system_);
    Rml::SetRenderInterface(&renderer_);
    if (!Rml::Initialise()) {
        Rml::SetRenderInterface(nullptr);
        Rml::SetSystemInterface(nullptr);
        return false;
    }
    initialized_ = true;
    if (!renderer_) {
        shutdown();
        return false;
    }
    resourceRoot_ = resourceRoot;

    const FontLoadResult fontResult = loadEditorFonts(resourceRoot);
    if (!fontResult.ok) {
        TraceLog(LOG_ERROR, "[editor] %s", fontResult.error.c_str());
        shutdown();
        return false;
    }

    context_ = Rml::CreateContext("editor", Rml::Vector2i(width, height));
    if (!context_) {
        shutdown();
        return false;
    }
    context_->SetDensityIndependentPixelRatio(dpRatio);
    renderer_.SetViewport(width, height);
    debuggerInitialized_ = Rml::Debugger::Initialise(context_);

    const std::filesystem::path fullDocumentPath = resourceRoot / documentPath;
    document_ = context_->LoadDocument(fullDocumentPath.string());
    if (document_) document_->Show();

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

    if (context_) {
        // The debugger owns private documents on this context. It must release
        // them itself before the host unloads application documents.
        if (debuggerInitialized_) {
            Rml::Debugger::Shutdown();
            debuggerInitialized_ = false;
            context_->Update();
        }
        // Unload is deferred by RmlUi. One final Update completes document
        // destruction while the context and custom interfaces are still alive.
        document_ = nullptr;
        context_->UnloadAllDocuments();
        context_->Update();
        Rml::RemoveContext("editor");
        context_ = nullptr;
    }

    // system_ and renderer_ are members declared before the runtime pointers;
    // they remain alive through this call, as required by RmlUi's interface
    // lifetime contract. Clear the global non-owning pointers only afterwards.
    Rml::Shutdown();
    Rml::SetRenderInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
    document_ = nullptr;
    resourceRoot_.clear();
    debuggerInitialized_ = false;
    debugger_ = false;
    initialized_ = false;
}

} // namespace ArtCade::EditorNative
