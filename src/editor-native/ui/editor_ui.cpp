#include "editor-native/ui/editor_ui.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/inspector_actions.h"
#include "editor-native/app/asset_import.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/commands/image_asset_commands.h"
#include "editor-native/commands/audio_asset_commands.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/project_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view_camera.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>

#include <raylib.h>   // SetClipboardText (Console copy)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ArtCade::EditorNative {

std::string escapeRml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            default:  out += c;        break;
        }
    }
    return out;
}

std::optional<AssetMenuKind> parseAssetMenuKind(const std::string& tag) {
    if (tag == "image")   return AssetMenuKind::Image;
    if (tag == "anim")    return AssetMenuKind::Animation;
    if (tag == "tileset") return AssetMenuKind::Tileset;
    if (tag == "audio")   return AssetMenuKind::Audio;
    if (tag == "font")    return AssetMenuKind::Font;
    return std::nullopt;
}

std::string assetDisplayName(const std::string& name, const std::string& assetId) {
    std::string display = name.empty() ? assetId : name;
    for (const std::string& suffix : {std::string(".anim"), std::string(".tileset")}) {
        if (display.size() > suffix.size()
            && display.compare(display.size() - suffix.size(), suffix.size(), suffix) == 0) {
            display.erase(display.size() - suffix.size());
            break;
        }
    }
    return display;
}

// ----------------------------------------------------------------------------
// The single RmlUi event listener. Attached to the document root; reads
// data-action / data-arg off the bubbled target and forwards to the coordinator.
// ----------------------------------------------------------------------------
namespace {

std::string attribute(Rml::Element* element, const char* name) {
    return element ? element->GetAttribute<Rml::String>(name, Rml::String()) : std::string();
}

std::string formValue(Rml::Element* element, Rml::Event& event) {
    if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element))
        return control->GetValue();
    return event.GetParameter<Rml::String>("value", Rml::String());
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string compactNumber(float value) {
    if (!std::isfinite(value)) return "";
    const float rounded = std::round(value);
    if (std::fabs(value - rounded) < 0.0005f) {
        return std::to_string(static_cast<int>(rounded));
    }
    std::string s = std::to_string(value);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::optional<BoxColliderMode> parseBoxColliderModeArg(const std::string& arg) {
    if (arg == "solid") return BoxColliderMode::Solid;
    if (arg == "trigger") return BoxColliderMode::Trigger;
    if (arg == "oneWayPlatform") return BoxColliderMode::OneWayPlatform;
    return std::nullopt;
}

bool sceneLayerNameExists(const SceneDef& scene, const std::string& name) {
    const std::string target = lower(name);
    for (const SceneLayerDef& layer : scene.layers) {
        if (lower(layer.name) == target) return true;
    }
    return false;
}

bool actionRequiresPendingEditGate(const std::string& action) {
    static constexpr std::string_view actions[] = {
        "new-project", "open-project", "save-project", "save-project-as",
        "play-project", "play-current-scene",
        "select-entity", "select-scene", "select-layer", "select-animation-clip",
        "open-sprite-animation", "close-sprite-animation",
        "open-tileset-editor", "close-tileset-editor",
        "open-scene-workspace", "open-logic-workspace",
        "toggle-inspector-section",
        "toggle-logic-rule-collapsed", "collapse-all-logic-rules", "expand-all-logic-rules",
    };
    return std::find(std::begin(actions), std::end(actions), action) != std::end(actions);
}

} // namespace

class EditorUi::Listener final : public Rml::EventListener {
public:
    explicit Listener(EditorUi& ui) : ui_(ui) {}

    void ProcessEvent(Rml::Event& event) override {
        const Rml::String type = event.GetType();
        std::string action, arg;
        Rml::Element* actionElement = nullptr;
        for (Rml::Element* e = event.GetTargetElement(); e; e = e->GetParentNode()) {
            action = (type == "dblclick") ? attribute(e, "data-dbl-action")
                                          : attribute(e, "data-action");
            if (!action.empty()) {
                arg = attribute(e, "data-arg");
                actionElement = e;
                break;
            }
        }
        // Baseline of the field being edited, captured at focus time. The live
        // "value" attribute cannot serve as the pre-edit reference for change
        // detection or Escape-restore: RmlUi's WidgetTextInput rewrites that
        // attribute on every keystroke, so it always equals the typed text.
        if (type == "focus") {
            const bool commitField = action.rfind("commit-", 0) == 0;
            focusBaselineElement_ = commitField ? actionElement : nullptr;
            focusBaselineValue_ = commitField ? formValue(actionElement, event)
                                              : std::string();
            return;
        }
        if (action.empty()) return;

        // Asset search is the only live text field: it is workspace state, not
        // authoring data, so every RmlUi change event becomes the narrow
        // SetAssetFilterIntent. Escape clears the same field and follows the
        // same path. The input itself lives in a stable slot and is not rebuilt
        // by the Assets list refresh.
        if (action == "set-asset-filter") {
            if (type == "keydown") {
                const int key = event.GetParameter<int>("key_identifier", 0);
                if (key != Rml::Input::KI_ESCAPE) return;
                if (auto* control =
                        rmlui_dynamic_cast<Rml::ElementFormControl*>(actionElement)) {
                    control->SetValue("");
                }
                ui_.handleAction(action, arg, {});
                event.StopPropagation();
                return;
            }
            if (type == "change") ui_.handleAction(action, arg, formValue(actionElement, event));
            return;
        }

        // Hierarchy context menu triggers carry the click position; the show is
        // deferred to processFrame (see requestHierarchyContextMenu).
        if (type == "click"
            && (action == "open-scene-menu" || action == "open-entity-menu")) {
            if (action == "open-entity-menu") ui_.handleAction("select-entity", arg, {});
            ui_.requestHierarchyContextMenu(
                action == "open-scene-menu" ? HierarchyMenuKind::Scene
                                            : HierarchyMenuKind::Entity,
                arg,
                static_cast<int>(event.GetParameter<float>("mouse_x", 0.f)),
                static_cast<int>(event.GetParameter<float>("mouse_y", 0.f)));
            return;
        }

        // Assets row menu trigger: arg is "kind|assetId" (same deferred show).
        if (type == "click" && action == "open-asset-menu") {
            const std::size_t sep = arg.find('|');
            std::optional<AssetMenuKind> kind;
            if (sep != std::string::npos) kind = parseAssetMenuKind(arg.substr(0, sep));
            if (kind) {
                ui_.requestAssetContextMenu(
                    *kind, arg.substr(sep + 1),
                    static_cast<int>(event.GetParameter<float>("mouse_x", 0.f)),
                    static_cast<int>(event.GetParameter<float>("mouse_y", 0.f)));
            }
            return;
        }

        const bool isCommit = action.rfind("commit-", 0) == 0;
        const bool isResize = action.rfind("resize-", 0) == 0;

        if (isResize) {
            if (type != "drag") return;
            ui_.handleDrag(action, event.GetParameter<float>("mouse_x", 0.f),
                                   event.GetParameter<float>("mouse_y", 0.f));
            return;
        }
        // Text edits stay local while typing. Commit only on blur or explicit Enter.
        if (isCommit) {
            const int key = event.GetParameter<int>("key_identifier", 0);
            if (type == "keydown" && key == Rml::Input::KI_ESCAPE) {
                if (action == "commit-layer-rename") {
                    ui_.handleAction("cancel-layer-rename", arg, formValue(actionElement, event));
                } else if (actionElement == focusBaselineElement_) {
                    if (auto* control =
                            rmlui_dynamic_cast<Rml::ElementFormControl*>(actionElement)) {
                        control->SetValue(focusBaselineValue_);
                    }
                }
                event.StopPropagation();
                return;
            }
            const bool enter = type == "keydown"
                && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER);
            if (type != "blur" && !enter) return;

            const std::string pendingValue = formValue(actionElement, event);
            const PendingEditResult pending = classifyPendingEdit(action, pendingValue);
            if (!pending.resolved()) {
                ui_.coordinator_.logError(pending.message);
                if (actionElement) actionElement->Focus(true);
                event.StopPropagation();
                return;
            }
            // Unchanged-value skip only when the focus-time baseline is known;
            // without one, commit - a no-op command is cheaper than losing the
            // edit. The baseline is consumed either way.
            const bool baselineKnown = actionElement
                && actionElement == focusBaselineElement_;
            focusBaselineElement_ = nullptr;
            if (baselineKnown
                && !pendingEditNeedsCommit(action, pendingValue, focusBaselineValue_)) {
                return;
            }
        }
        if (!isCommit && type != "click" && type != "dblclick") return;

        if (type == "click" && action == "select-layer" && isLayerDoubleClick(arg)) {
            action = "begin-layer-rename";
        }

        // A button/entry standing in for what used to be a <select> option
        // (Logic Board's Event/Action/Key/Object-Type pickers) carries the
        // value it represents in data-value, separate from data-arg (which
        // stays the addressing key — a ruleId, or "ruleId|actionIndex").
        const std::string value = (actionElement && actionElement->HasAttribute("data-value"))
            ? attribute(actionElement, "data-value")
            : formValue(actionElement, event);
        ui_.handleAction(action, arg, value);
    }

private:
    bool isLayerDoubleClick(const std::string& layerId) {
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        const bool repeated = layerId == lastLayerClickId_
            && (now - lastLayerClickTime_) <= std::chrono::milliseconds(500);
        lastLayerClickId_ = layerId;
        lastLayerClickTime_ = now;
        return repeated;
    }

    EditorUi& ui_;
    std::string lastLayerClickId_;
    std::chrono::steady_clock::time_point lastLayerClickTime_{};
    // Pre-edit value of the commit field being edited (set on focus). The
    // element pointer is only ever compared, never dereferenced: a rebuild
    // invalidates it, and the next focus event re-stamps the stash.
    Rml::Element* focusBaselineElement_ = nullptr;
    std::string   focusBaselineValue_;
};

// ----------------------------------------------------------------------------
EditorUi::EditorUi(EditorCoordinator& coordinator, Rml::ElementDocument* document,
                   Rml::ElementDocument* animationDocument,
                   Rml::ElementDocument* tilesetDocument)
    : coordinator_(coordinator),
      document_(document),
      animationDocument_(animationDocument),
      tilesetDocument_(tilesetDocument),
      spriteAnimationEditor_(coordinator, animationDocument),
      tilesetEditor_(coordinator, tilesetDocument),
      logicBoardEditor_(coordinator, document) {}

EditorUi::~EditorUi() { detach(); }

void EditorUi::bind() {
    // A second bind must never register a duplicate copy of the listener.
    // After detach the document pointers are deliberately invalidated, so bind
    // remains a safe no-op instead of resurrecting a closed document session.
    if (listener_ || !document_) return;
    listener_ = std::make_unique<Listener>(*this);
    const auto bindDocument = [&](Rml::ElementDocument* doc) {
        if (!doc) return;
        doc->AddEventListener("click", listener_.get());
        doc->AddEventListener("dblclick", listener_.get());
        doc->AddEventListener("focus", listener_.get(), true);
        doc->AddEventListener("blur", listener_.get(), true);
        doc->AddEventListener("keydown", listener_.get(), true);
        doc->AddEventListener("change", listener_.get());
        doc->AddEventListener("drag", listener_.get());
    };
    bindDocument(document_);
    bindDocument(animationDocument_);
    bindDocument(tilesetDocument_);

    // Initial full paint of every panel.
    coordinator_.consumeInvalidations();
    applyInvalidations(EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                       | EditorInvalidation::Console  | EditorInvalidation::Toolbar
                       | EditorInvalidation::Assets   | EditorInvalidation::Layout
                       | EditorInvalidation::LogicBoard);
    updateZoomReadout();   // initial paint (zoom % is Viewport-driven, not in the set)
}

void EditorUi::detach() {
    if (listener_) {
        const auto detachDocument = [&](Rml::ElementDocument* doc) {
            if (!doc) return;
            doc->RemoveEventListener("click", listener_.get());
            doc->RemoveEventListener("dblclick", listener_.get());
            doc->RemoveEventListener("focus", listener_.get(), true);
            doc->RemoveEventListener("blur", listener_.get(), true);
            doc->RemoveEventListener("keydown", listener_.get(), true);
            doc->RemoveEventListener("change", listener_.get());
            doc->RemoveEventListener("drag", listener_.get());
        };
        detachDocument(document_);
        detachDocument(animationDocument_);
        detachDocument(tilesetDocument_);
        // RmlUi completes EventListener::OnDetach synchronously in each remove;
        // only then is the listener destroyed.
        listener_.reset();
    }

    pendingHierarchyMenu_.reset();
    pendingAssetMenu_.reset();
    viewportContextMenuVisible_ = false;
    hierarchyContextMenuVisible_ = false;
    assetsContextMenuVisible_ = false;
    newProjectRequest_ = {};
    openProjectRequest_ = {};
    saveProjectRequest_ = {};
    saveProjectAsRequest_ = {};
    importAssetRequest_ = {};
    addEntityRequest_ = {};
    addInstanceRequest_ = {};
    createEntityHereRequest_ = {};
    createInstanceHereRequest_ = {};
    fitViewRequest_ = {};
    spriteAnimationEditor_.detach();
    tilesetEditor_.detach();
    logicBoardEditor_.detach();

    // These are non-owning and become invalid as soon as the host unloads its
    // documents. Null them even when bind never succeeded (missing document).
    document_ = nullptr;
    animationDocument_ = nullptr;
    tilesetDocument_ = nullptr;
}

void EditorUi::processFrame() {
    if (!listener_ || !document_) return;
    applyInvalidations(coordinator_.consumeInvalidations());
    // Deferred context menus: shown here, after the application's
    // outside-click check for this frame has already run.
    showPendingHierarchyMenu();
    showPendingAssetMenu();
    // The preview playhead advances without invalidation (workspace tick), so
    // the timeline highlight and Play/Pause affordance follow it live here,
    // class-only — never via a markup rebuild that would steal input focus.
    spriteAnimationEditor_.updatePlayhead();
}

PendingEditResult EditorUi::resolvePendingEdits() {
    Rml::Context* visited[3] = {nullptr, nullptr, nullptr};
    std::size_t visitedCount = 0;
    Rml::ElementDocument* documents[] = {document_, animationDocument_, tilesetDocument_};

    for (Rml::ElementDocument* document : documents) {
        Rml::Context* context = document ? document->GetContext() : nullptr;
        if (!context) continue;
        if (std::find(visited, visited + visitedCount, context) != visited + visitedCount) continue;
        visited[visitedCount++] = context;

        Rml::Element* focus = context->GetFocusElement();
        const std::string action = attribute(focus, "data-action");
        if (!focus || action.rfind("commit-", 0) != 0) continue;

        auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(focus);
        const std::string value = control ? control->GetValue() : attribute(focus, "value");
        const PendingEditResult classified = classifyPendingEdit(action, value);
        if (!classified.resolved()) {
            coordinator_.logError(classified.message);
            focus->Focus(true);
            return classified;
        }

        // Blur is the canonical commit path already used for ordinary editing;
        // the bound listener synchronously dispatches the same action/parser.
        focus->Blur();

        // A semantic rejection may rebuild and refocus a commit field (notably
        // duplicate layer names). Treat that as unresolved and block the caller.
        Rml::Element* remainingFocus = context->GetFocusElement();
        if (remainingFocus
            && attribute(remainingFocus, "data-action").rfind("commit-", 0) == 0) {
            const PendingEditResult rejected{
                PendingEditStatus::Invalid,
                "The focused edit could not be applied"};
            coordinator_.logError(rejected.message);
            remainingFocus->Focus(true);
            return rejected;
        }
        return {};
    }
    return {};
}

void EditorUi::applyInvalidations(EditorInvalidation flags) {
    if (flags == EditorInvalidation::None) return;
    if (has(flags, EditorInvalidation::Hierarchy) || has(flags, EditorInvalidation::Project))
        hierarchy_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Inspector)) {
        inspector_.refresh(document_, coordinator_);
        inspector_.consumeInspectorReveal(document_, coordinator_);
    }
    if (has(flags, EditorInvalidation::Console))
        console_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Assets) || has(flags, EditorInvalidation::Project))
        assets_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::LogicBoard) || has(flags, EditorInvalidation::Project))
        logicBoardEditor_.refresh();
    if (has(flags, EditorInvalidation::Viewport) || has(flags, EditorInvalidation::Assets)
        || has(flags, EditorInvalidation::Project)) {
        spriteAnimationEditor_.refresh();
        tilesetEditor_.refresh();
    }
    if (has(flags, EditorInvalidation::Toolbar))
        refreshToolbar();
    if (has(flags, EditorInvalidation::Toolbar) || has(flags, EditorInvalidation::Project)
        || has(flags, EditorInvalidation::LogicBoard))
        refreshStatusBar();
    if (has(flags, EditorInvalidation::Viewport)) {
        updateZoomReadout();
        tilesetEditor_.updateZoomReadout();
    }
    if (has(flags, EditorInvalidation::Layout))
        refreshLayout();
    if (has(flags, EditorInvalidation::LogicBoard) || has(flags, EditorInvalidation::Viewport)
        || has(flags, EditorInvalidation::Project))
        refreshCenterWorkspace();
}

void EditorUi::refreshCenterWorkspace() {
    if (!document_) return;
    const bool logic = coordinator_.state().logicBoardEditor.mode == CenterWorkspaceMode::Logic;
    if (Rml::Element* el = document_->GetElementById("viewport")) el->SetClass("hidden", logic);
    if (Rml::Element* el = document_->GetElementById("logic-board-panel")) el->SetClass("hidden", !logic);
    if (Rml::Element* el = document_->GetElementById("center-tab-scene")) el->SetClass("active", !logic);
    if (Rml::Element* el = document_->GetElementById("center-tab-logic")) el->SetClass("active", logic);
    if (Rml::Element* el = document_->GetElementById("scene-context-tools")) el->SetClass("hidden", logic);
    if (Rml::Element* el = document_->GetElementById("logic-context-tools")) el->SetClass("hidden", !logic);
    // Rules are edited inline in the board. Keeping the entity Inspector visible
    // would present a second, unrelated target (instance vs owning Object Type).
    // Its inline width remains untouched and is restored automatically in Scene.
    if (Rml::Element* el = document_->GetElementById("split-right")) el->SetClass("hidden", logic);
    if (Rml::Element* el = document_->GetElementById("right-col")) el->SetClass("hidden", logic);
}

void EditorUi::refreshLayout() {
    if (!document_) return;
    const bool consoleVisible = coordinator_.uiState().consoleVisible;
    if (Rml::Element* console = document_->GetElementById("console"))
        console->SetClass("hidden", !consoleVisible);
    if (Rml::Element* splitter = document_->GetElementById("split-console"))
        splitter->SetClass("hidden", !consoleVisible);
}









SceneId EditorUi::currentViewSceneId() const {
    return (coordinator_.isPlaying() && coordinator_.playSession())
        ? coordinator_.playSession()->sceneId()
        : coordinator_.state().activeSceneId;
}

void EditorUi::updateZoomReadout() {
    if (!document_) return;
    Rml::Element* el = document_->GetElementById("toolbar-zoom");
    if (!el) return;
    const int pct = static_cast<int>(
        coordinator_.sceneView(currentViewSceneId()).zoom * 100.f + 0.5f);
    el->SetInnerRML(std::to_string(pct) + "%");
}

bool EditorUi::isPlaying() const { return coordinator_.isPlaying(); }

void EditorUi::setProjectFileHandlers(ProjectFileRequest newProject,
                                      ProjectFileRequest open,
                                      ProjectFileRequest save,
                                      ProjectFileRequest saveAs) {
    newProjectRequest_    = std::move(newProject);
    openProjectRequest_   = std::move(open);
    saveProjectRequest_   = std::move(save);
    saveProjectAsRequest_ = std::move(saveAs);
}

void EditorUi::setImportHandler(ImportAssetRequest importAsset) {
    importAssetRequest_ = std::move(importAsset);
}

void EditorUi::setImportImageForAnimationHandler(ImportImageRequest importImage) {
    spriteAnimationEditor_.setImportImageRequest(std::move(importImage));
}

void EditorUi::setFitViewHandler(WorkspaceRequest fitView) {
    fitViewRequest_ = std::move(fitView);
}

void EditorUi::setAnimationSliceHandler(WorkspaceRequest sliceAnimation) {
    spriteAnimationEditor_.setSliceRequest(std::move(sliceAnimation));
}

void EditorUi::setTilesetApplySlicingHandler(WorkspaceRequest applyTilesetSlicing) {
    tilesetEditor_.setApplySlicingRequest(std::move(applyTilesetSlicing));
}

void EditorUi::setTilesetCloseHandler(WorkspaceRequest closeTileset) {
    tilesetEditor_.setCloseRequest(std::move(closeTileset));
}

void EditorUi::setCreateTilesetFromImageHandler(CreateTilesetRequest createTileset) {
    tilesetEditor_.setCreateFromImageRequest(std::move(createTileset));
}

void EditorUi::setTilesetImageSizeProvider(ImageSizeProvider imageSize) {
    tilesetEditor_.setImageSizeProvider(std::move(imageSize));
}

void EditorUi::setEntityPlacementHandlers(EntityPlacementRequest addEntity,
                                          EntityPlacementRequest addInstance,
                                          EntityPlacementRequest createEntityHere,
                                          EntityPlacementRequest createInstanceHere) {
    addEntityRequest_ = std::move(addEntity);
    addInstanceRequest_ = std::move(addInstance);
    createEntityHereRequest_ = std::move(createEntityHere);
    createInstanceHereRequest_ = std::move(createInstanceHere);
}

void EditorUi::showViewportContextMenu(int physicalX, int physicalY,
                                       bool canCreateInstance) {
    if (!document_) return;
    hideContextMenus();   // only one context menu open at a time
    if (Rml::Element* menu = document_->GetElementById("viewport-context-menu")) {
        menu->SetProperty("left", std::to_string(physicalX) + "px");
        menu->SetProperty("top", std::to_string(physicalY) + "px");
        menu->SetClass("hidden", false);
        viewportContextMenuVisible_ = true;
    }
    if (Rml::Element* item = document_->GetElementById("ctx-create-instance")) {
        item->SetClass("hidden", !canCreateInstance);
    }
}

void EditorUi::requestHierarchyContextMenu(HierarchyMenuKind kind, std::string targetId,
                                           int physicalX, int physicalY) {
    pendingHierarchyMenu_ = PendingHierarchyMenu{kind, std::move(targetId),
                                                 physicalX, physicalY};
}

void EditorUi::requestAssetContextMenu(AssetMenuKind kind, std::string assetId,
                                       int physicalX, int physicalY) {
    pendingAssetMenu_ = PendingAssetMenu{kind, std::move(assetId), physicalX, physicalY};
}

void EditorUi::showPendingAssetMenu() {
    if (!pendingAssetMenu_ || !document_) {
        pendingAssetMenu_.reset();
        return;
    }
    const PendingAssetMenu request = *pendingAssetMenu_;
    pendingAssetMenu_.reset();
    // Every entry mutates the authoring document or opens an authoring editor;
    // during Play the menu has nothing to offer (parity with the hierarchy menu).
    if (coordinator_.isPlaying()) return;
    Rml::Element* menu = document_->GetElementById("assets-context-menu");
    if (!menu) return;
    hideContextMenus();

    const ProjectDoc& doc = coordinator_.document().data();
    const bool isImage   = request.kind == AssetMenuKind::Image;
    const bool isAnim    = request.kind == AssetMenuKind::Animation;
    const bool isTileset = request.kind == AssetMenuKind::Tileset;

    // Re-resolve at show time: the clicked row may be stale by one frame.
    // For derived assets also resolve the source image, offered for re-derivation
    // ("New ... from Source Image") since consumed images have no row of their own.
    bool exists = false;
    AssetId sourceImageId;
    switch (request.kind) {
        case AssetMenuKind::Image:
            exists = coordinator_.document().hasImageAsset(request.assetId);
            break;
        case AssetMenuKind::Animation:
            for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
                if (asset.id != request.assetId) continue;
                exists = true;
                if (!asset.clips.empty()) sourceImageId = asset.clips.front().imageId;
                break;
            }
            break;
        case AssetMenuKind::Tileset:
            if (const TilesetAsset* ts = coordinator_.document().findTilesetAsset(request.assetId)) {
                exists = true;
                sourceImageId = ts->imageAssetId;
            }
            break;
        case AssetMenuKind::Audio:
            for (const AudioAssetDef& asset : doc.audioAssets)
                if (asset.assetId == request.assetId) { exists = true; break; }
            break;
        case AssetMenuKind::Font:
            for (const FontAssetDef& asset : doc.fontAssets)
                if (asset.assetId == request.assetId) { exists = true; break; }
            break;
    }
    if (!exists) return;

    // Entries are stamped with the same actions the panel's old always-visible
    // buttons dispatched — menu and buttons share one semantic operation path.
    const auto setEntry = [&](const char* id, bool shown, const char* action,
                              const std::string& arg) {
        if (Rml::Element* entry = document_->GetElementById(id)) {
            entry->SetClass("hidden", !shown);
            if (shown) {
                entry->SetAttribute("data-action", action);
                entry->SetAttribute("data-arg", arg);
            }
        }
    };
    const bool hasSource = (isAnim || isTileset) && !sourceImageId.empty()
        && coordinator_.document().hasImageAsset(sourceImageId);
    setEntry("actx-edit", isAnim || isTileset,
             isAnim ? "open-sprite-animation" : "open-tileset-editor", request.assetId);
    setEntry("actx-use", isImage || isAnim,
             isImage ? "set-sprite-asset" : "set-sprite-animation", request.assetId);
    setEntry("actx-make-anim",   isImage, "create-sprite-animation",   request.assetId);
    setEntry("actx-make-tileset", isImage, "create-tileset-from-image", request.assetId);
    setEntry("actx-src-anim",    hasSource, "create-sprite-animation",   sourceImageId);
    setEntry("actx-src-tileset", hasSource, "create-tileset-from-image", sourceImageId);
    const char* removeAction =
        isImage   ? "remove-image-asset"
      : isAnim    ? "remove-sprite-animation"
      : isTileset ? "remove-tileset"
      : request.kind == AssetMenuKind::Audio ? "remove-audio-asset"
                                             : "remove-font-asset";
    setEntry("actx-delete", true, removeAction, request.assetId);

    menu->SetProperty("left", std::to_string(request.x) + "px");
    menu->SetProperty("top",  std::to_string(request.y) + "px");
    menu->SetClass("hidden", false);
    assetsContextMenuVisible_ = true;
}

void EditorUi::showPendingHierarchyMenu() {
    if (!pendingHierarchyMenu_ || !document_) {
        pendingHierarchyMenu_.reset();
        return;
    }
    const PendingHierarchyMenu request = *pendingHierarchyMenu_;
    pendingHierarchyMenu_.reset();
    // Every entry mutates the authoring document; while Play runs the menu has
    // nothing to offer (the coordinator would reject the commands anyway).
    if (coordinator_.isPlaying()) return;
    Rml::Element* menu = document_->GetElementById("hierarchy-context-menu");
    if (!menu) return;
    hideContextMenus();

    const bool sceneKind = request.kind == HierarchyMenuKind::Scene;
    const bool alreadyStart =
        sceneKind && coordinator_.document().startSceneId() == request.targetId;
    // Clone/Delete mutate the instance itself - omitted for a locked-layer
    // entity, the same way they're omitted entirely for a scene row.
    bool entityLocked = false;
    if (!sceneKind) {
        const EntityId targetEntity =
            static_cast<EntityId>(std::strtoul(request.targetId.c_str(), nullptr, 10));
        if (const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
                coordinator_.state().activeSceneId, targetEntity)) {
            entityLocked = coordinator_.document().isInstanceLayerLocked(
                coordinator_.state().activeSceneId, *inst);
        }
    }
    const auto setEntry = [&](const char* id, bool shown) {
        if (Rml::Element* entry = document_->GetElementById(id)) {
            entry->SetClass("hidden", !shown);
            entry->SetAttribute("data-arg", request.targetId);
        }
    };
    setEntry("hctx-set-start",     sceneKind && !alreadyStart);
    setEntry("hctx-del-scene",     sceneKind);
    setEntry("hctx-add-instance",  !sceneKind);
    setEntry("hctx-clone-entity",  !sceneKind && !entityLocked);
    setEntry("hctx-del-entity",    !sceneKind && !entityLocked);

    menu->SetProperty("left", std::to_string(request.x) + "px");
    menu->SetProperty("top",  std::to_string(request.y) + "px");
    menu->SetClass("hidden", false);
    hierarchyContextMenuVisible_ = true;
}

void EditorUi::hideContextMenus() {
    if (!document_) return;
    if (Rml::Element* menu = document_->GetElementById("viewport-context-menu")) {
        menu->SetClass("hidden", true);
    }
    if (Rml::Element* menu = document_->GetElementById("hierarchy-context-menu")) {
        menu->SetClass("hidden", true);
    }
    if (Rml::Element* menu = document_->GetElementById("assets-context-menu")) {
        menu->SetClass("hidden", true);
    }
    if (Rml::Element* menu = document_->GetElementById("logic-type-menu")) {
        menu->SetClass("hidden", true);
    }
    if (Rml::Element* trigger = document_->GetElementById("logic-type-trigger")) {
        trigger->SetClass("open", false);
    }
    viewportContextMenuVisible_ = false;
    hierarchyContextMenuVisible_ = false;
    assetsContextMenuVisible_ = false;
    logicTypeMenuVisible_ = false;
}

bool EditorUi::isContextMenuHit(int physicalX, int physicalY) const {
    if (!document_) return false;
    const auto hits = [&](const char* id, bool visible) {
        if (!visible) return false;
        Rml::Element* menu = document_->GetElementById(id);
        if (!menu) return false;
        const Rml::Vector2f offset = menu->GetAbsoluteOffset();
        const float left = offset.x;
        const float top = offset.y;
        const float right = left + menu->GetClientWidth();
        const float bottom = top + menu->GetClientHeight();
        const float x = static_cast<float>(physicalX);
        const float y = static_cast<float>(physicalY);
        return x >= left && x < right && y >= top && y < bottom;
    };
    return hits("viewport-context-menu", viewportContextMenuVisible_)
        || hits("hierarchy-context-menu", hierarchyContextMenuVisible_)
        || hits("assets-context-menu", assetsContextMenuVisible_)
        || hits("logic-type-menu", logicTypeMenuVisible_);
}

void EditorUi::toggleLogicTypeMenu() {
    if (!document_) return;
    if (logicTypeMenuVisible_) { hideContextMenus(); return; }
    Rml::Element* trigger = document_->GetElementById("logic-type-trigger");
    Rml::Element* menu = document_->GetElementById("logic-type-menu");
    if (!trigger || !menu) return;
    hideContextMenus();
    menu->SetInnerRML(logicBoardEditor_.objectTypeMenuEntries());
    const Rml::Vector2f offset = trigger->GetAbsoluteOffset();
    menu->SetProperty("left", std::to_string(static_cast<int>(offset.x)) + "px");
    menu->SetProperty("top",
        std::to_string(static_cast<int>(offset.y + trigger->GetClientHeight())) + "px");
    menu->SetProperty("width", std::to_string(static_cast<int>(trigger->GetClientWidth())) + "px");
    menu->SetClass("hidden", false);
    trigger->SetClass("open", true);
    logicTypeMenuVisible_ = true;
}

void EditorUi::refreshToolbar() {
    if (!document_) return;
    const bool playing = coordinator_.isPlaying();
    const bool logicWorkspace =
        coordinator_.state().logicBoardEditor.mode == CenterWorkspaceMode::Logic;
    // Entering Play freezes authoring: an open context menu must not linger.
    if (playing) hideContextMenus();

    // Play affordances derive straight from the authorities — never stored.
    const auto setEnabled = [&](const char* id, bool enabled) {
        if (Rml::Element* el = document_->GetElementById(id))
            el->SetClass("disabled", !enabled);
    };
    // A toolbar control and its View/Edit-menu twin are two entry points for
    // one canonical action, so they always share one enabled/active value —
    // set both from a single call instead of repeating each condition twice.
    const auto setEnabledBoth = [&](const char* toolbarId, const char* menuId, bool enabled) {
        setEnabled(toolbarId, enabled);
        setEnabled(menuId, enabled);
    };
    const auto setActiveBoth = [&](const char* toolbarId, const char* menuId, bool active) {
        if (Rml::Element* el = document_->GetElementById(toolbarId)) el->SetClass("active", active);
        if (Rml::Element* el = document_->GetElementById(menuId))    el->SetClass("active", active);
    };
    const LogicBoardEditorState& logicView = coordinator_.state().logicBoardEditor;
    const EntityDef* logicType = logicView.objectTypeId
        ? coordinator_.document().findObjectType(*logicView.objectTypeId) : nullptr;
    const bool hasLogicBoard = logicType && logicType->logicBoard.has_value();
    setEnabled("btn-logic-add-rule",
               logicWorkspace && !playing && hasLogicBoard
               && logicView.tab == LogicBoardTab::Rules);
    setEnabled("btn-logic-rules", logicWorkspace);
    setEnabled("btn-logic-lua", logicWorkspace);
    setEnabled("btn-logic-validate", logicWorkspace && hasLogicBoard);
    if (Rml::Element* el = document_->GetElementById("btn-logic-rules"))
        el->SetClass("active", logicWorkspace && logicView.tab == LogicBoardTab::Rules);
    if (Rml::Element* el = document_->GetElementById("btn-logic-lua"))
        el->SetClass("active", logicWorkspace && logicView.tab == LogicBoardTab::GeneratedLua);
    if (Rml::Element* search = document_->GetElementById("logic-toolbar-search")) {
        if (Rml::Context* context = document_->GetContext();
            !context || context->GetFocusElement() != search) {
            if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(search))
                control->SetValue(logicView.search);
        }
    }
    setEnabled("btn-play-project", !playing && coordinator_.canPlayProject());
    setEnabled("btn-play-scene",   !playing && coordinator_.canPlayCurrentScene());
    setEnabled("btn-stop",         playing);
    // Undo/Redo are derived affordances: available only with history and outside Play.
    setEnabledBoth("btn-undo", "menu-undo", !playing && coordinator_.canUndo());
    setEnabledBoth("btn-redo", "menu-redo", !playing && coordinator_.canRedo());
    // Select/Pan are always present, unlike the Tilemap tools (Brush/Eraser/
    // Picker/Rectangle/Fill), which only render inside a selected tilemap's
    // own Inspector section - both read and write the same EditorState
    // ::activeTool, no second local state.
    {
        const bool toolActionable = !playing && !logicWorkspace
            && coordinator_.document().findScene(coordinator_.state().activeSceneId) != nullptr;
        const EditorTool activeTool = coordinator_.state().activeTool;
        setEnabled("btn-tool-select", toolActionable);
        setEnabled("btn-tool-pan", toolActionable);
        if (Rml::Element* el = document_->GetElementById("btn-tool-select"))
            el->SetClass("active", toolActionable && activeTool == EditorTool::Select);
        if (Rml::Element* el = document_->GetElementById("btn-tool-pan"))
            el->SetClass("active", toolActionable && activeTool == EditorTool::Pan);
    }
    {
        const EntityId primarySel = coordinator_.selection().primaryEntity;
        const SceneInstanceDef* selInst = primarySel != INVALID_ENTITY
            ? coordinator_.document().findInstanceInScene(
                  coordinator_.state().activeSceneId, primarySel)
            : nullptr;
        const bool selEntityLocked = selInst
            && coordinator_.document().isInstanceLayerLocked(
                   coordinator_.state().activeSceneId, *selInst);
        setEnabled("menu-delete-entity",
                  !playing && primarySel != INVALID_ENTITY && !selEntityLocked);
    }

    const bool hasScene = coordinator_.document().findScene(
        coordinator_.state().activeSceneId) != nullptr;
    // Fit and the Grid/Snap toggles all act on the active scene's view state:
    // with no scene there is nothing to frame or draw a grid over, so a click
    // would be a no-op the coordinator now also rejects (defence in depth,
    // not just a greyed-out button).
    const bool gridActionable = !playing && !logicWorkspace && hasScene;
    setEnabledBoth("btn-fit-view",     "menu-fit-view",     gridActionable);
    setEnabledBoth("btn-grid-visible", "menu-grid-visible", gridActionable);
    // Zoom, unlike Grid/Snap/Fit, tracks the PlaySession's scene while
    // playing (see the zoom-in/out and reset-zoom handlers) — Play always has
    // a real scene, so it stays available then. Only truly nothing-to-zoom
    // (no scene, not playing) disables it.
    const bool canZoom = !logicWorkspace && (playing || hasScene);
    setEnabledBoth("btn-zoom-in",  "menu-zoom-in",   canZoom);
    setEnabledBoth("btn-zoom-out", "menu-zoom-out",  canZoom);
    setEnabledBoth("toolbar-zoom", "menu-reset-zoom", canZoom);

    // Central Scene View empty state: shown only when no scene exists to edit.
    if (Rml::Element* empty = document_->GetElementById("viewport-empty")) {
        empty->SetClass("hidden", hasScene || playing || logicWorkspace);
    }

    const EditorSceneViewState& view = coordinator_.sceneView(currentViewSceneId());
    const SceneId viewSceneId = currentViewSceneId();
    const SceneGridPresentation gridPres =
        makeSceneGridPresentation(coordinator_.document(), coordinator_.state(), viewSceneId);
    const bool tilemapGridMode =
        gridPres.kind == SceneGridKind::Tilemap && isTilemapTool(coordinator_.state().activeTool);
    const bool sceneSnapActionable = gridActionable && !tilemapGridMode;

    setActiveBoth("btn-grid-visible", "menu-grid-visible", view.gridVisible && gridActionable);
    setActiveBoth("btn-grid-snap", "menu-grid-snap",
                  view.gridSnapEnabled && sceneSnapActionable);
    setEnabledBoth("btn-grid-snap", "menu-grid-snap", sceneSnapActionable);
    setEnabled("btn-grid-size", gridActionable);

    std::string gridSizeLabel;
    if (tilemapGridMode) {
        gridSizeLabel = compactNumber(gridPres.cellSize.x) + " \xC3\x97 "
                      + compactNumber(gridPres.cellSize.y);
    } else {
        gridSizeLabel = compactNumber(gridPres.cellSize.x);
    }
    if (Rml::Element* el = document_->GetElementById("btn-grid-size")) {
        el->SetClass("grid-size-nav", tilemapGridMode);
        el->SetClass("grid-size-trigger", !tilemapGridMode);
        if (tilemapGridMode) {
            el->SetAttribute("data-action", "reveal-tilemap-cell-size");
            el->SetAttribute("title", "Open Tilemap cell size in Inspector");
            el->SetInnerRML(escapeRml(gridSizeLabel)
                            + " <span class=\"grid-nav-icon\">&#x2197;</span>");
        } else {
            el->RemoveAttribute("data-action");
            el->SetAttribute("title", "Grid Cell Size");
            el->SetInnerRML(escapeRml(gridSizeLabel)
                            + " <span class=\"icon-caret\">&#xeb5d;</span>");
        }
    }
    if (Rml::Element* el = document_->GetElementById("grid-size-control"))
        el->SetClass("disabled", !gridActionable || tilemapGridMode);
    if (Rml::Element* el = document_->GetElementById("grid-size-title")) {
        const std::string title = tilemapGridMode
            ? "Grid: Tilemap \xC2\xB7 " + gridPres.toolbarContextName
            : "Grid: World";
        el->SetInnerRML(escapeRml(title));
        if (!gridPres.toolbarTooltip.empty()) el->SetAttribute("title", gridPres.toolbarTooltip);
        else el->RemoveAttribute("title");
    }
    if (Rml::Element* el = document_->GetElementById("grid-cell-size-input")) {
        el->SetAttribute("value", gridSizeLabel);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(el))
            control->SetValue(gridSizeLabel);
    }
}

void EditorUi::refreshStatusBar() {
    if (!document_) return;
    const bool playing = coordinator_.isPlaying();
    const bool logicWorkspace =
        coordinator_.state().logicBoardEditor.mode == CenterWorkspaceMode::Logic;

    std::string contextText = playing ? "Play" : "Edit";
    if (logicWorkspace) {
        contextText += "  |  Logic";
        if (coordinator_.state().logicBoardEditor.objectTypeId)
            contextText += "  |  " + *coordinator_.state().logicBoardEditor.objectTypeId;
        if (playing) contextText += " (read-only)";
    } else {
        const SceneDef* scene = nullptr;
        if (playing && coordinator_.playSession()) {
            contextText += "  |  " + coordinator_.playSession()->scene().name;
        } else {
            scene = coordinator_.document().findScene(coordinator_.state().activeSceneId);
            if (scene) contextText += "  |  " + scene->name;
        }
        if (!playing && scene) {
            const std::string layerId =
                coordinator_.activeLayerId(coordinator_.state().activeSceneId);
            for (const SceneLayerDef& layer : scene->layers) {
                if (layer.id == layerId) {
                    contextText += "  |  " + layer.name;
                    break;
                }
            }
        }
    }
    if (Rml::Element* context = document_->GetElementById("status-context"))
        context->SetInnerRML(escapeRml(contextText));

    std::string projectName = coordinator_.document().data().projectName;
    if (projectName.empty()) projectName = "Untitled";
    const bool dirty = coordinator_.document().isDirty();
    if (dirty) projectName += "  *";
    if (Rml::Element* project = document_->GetElementById("status-project")) {
        project->SetInnerRML(escapeRml(projectName));
        project->SetClass("dirty", dirty);
    }
}

bool EditorUi::copySelectedConsoleMessage() {
    const ConsoleMessage* message = coordinator_.consoleMessage(console_.selectedIndex());
    if (!message) return false;
    SetClipboardText(formatConsoleMessageForClipboard(*message).c_str());
    return true;
}

void EditorUi::beginActiveSceneLayerRename() {
    inspector_.beginActiveSceneLayerRename(document_, coordinator_);
}

void EditorUi::showEntityPositionPreview(EntityId entity, Vec2 position) {
    inspector_.showEntityPositionPreview(document_, coordinator_, entity, position);
}

void EditorUi::showViewportPointerReadout(const ViewportPointerReadout& readout) {
    if (!document_) return;
    const std::string text = readout.valid ? formatViewportPointerReadout(readout) : std::string();
    if (text == pointerReadout_) return;
    pointerReadout_ = text;
    if (Rml::Element* el = document_->GetElementById("status-coords")) {
        el->SetInnerRML(escapeRml(text));
    }
}

void EditorUi::commitGridCellSize(const std::string& text) {
    if (coordinator_.isPlaying()) {
        coordinator_.logWarning("Stop Play before editing the grid");
        return;
    }
    const std::optional<float> parsed = parseNumberField(text);
    if (!parsed.has_value()) {
        coordinator_.logError("Grid cell size is not a number");
        return;
    }
    coordinator_.apply(SetSceneGridCellSizeIntent{coordinator_.state().activeSceneId, *parsed});
}

void EditorUi::handleAction(const std::string& action, const std::string& arg,
                            const std::string& value) {
    if (actionRequiresPendingEditGate(action) && !resolvePendingEdits().resolved()) return;

    const EntityId selected = coordinator_.selection().primaryEntity;

    // An action arriving while the Assets row menu is open was either picked
    // from it or makes it stale (shortcut, double-click): close it. Clicks that
    // miss the menu are already dismissed by the application's outside-click
    // check, and the hierarchy/viewport menus close inside their own entries.
    if (assetsContextMenuVisible_) hideContextMenus();
    // Same staleness guard for the Logic Board's Object Type menu: any other
    // action (switching tabs, selecting a different entity, entering Play,
    // toggling a different dropdown) makes it stale. Re-toggling the same
    // trigger is handled by toggleLogicTypeMenu() itself, below.
    if (logicTypeMenuVisible_
        && !(action == "toggle-logic-dropdown" && arg == "object-type")) {
        hideContextMenus();
    }

    // Inspector Add Component menu: toggle it open/closed, and close it whenever a
    // component is actually added (the add invalidates the Inspector, which then
    // re-renders without the menu). The coordinator still guards the commands.
    if (action == "toggle-add-component") {
        if (!coordinator_.isPlaying()) inspector_.toggleAddMenu(document_, coordinator_);
        return;
    }
    if (action == "add-sprite-renderer" || action == "add-box-collider"
        || action == "add-linear-mover" || action == "add-top-down"
        || action == "add-platformer" || action == "add-tilemap-component") {
        inspector_.closeAddMenu();   // then fall through to execute the add
    }

    // Inspector value dropdowns (Layer / Source / Tileset) follow the same
    // pattern: toggle here, close when an entry commits a pick (the pick
    // invalidates the Inspector, which re-renders with the list collapsed).
    if (action == "toggle-inspector-dropdown") {
        if (!coordinator_.isPlaying()) inspector_.toggleDropdown(document_, coordinator_, arg);
        return;
    }
    if (action == "set-entity-layer" || action == "set-sprite-asset"
        || action == "set-sprite-animation" || action == "set-tilemap-tileset") {
        inspector_.closeDropdowns();   // then fall through to execute the pick
    }

    // Logic Board's per-rule Key picker follows the same in-flow pattern as
    // the Inspector's dropdowns; its Object Type picker (in the fixed header,
    // never inside a scrollable list) instead uses a floating menu, same as
    // the Hierarchy/Assets context menus.
    if (action == "toggle-logic-dropdown") {
        if (coordinator_.isPlaying()) return;
        if (arg == "object-type") toggleLogicTypeMenu();
        else logicBoardEditor_.toggleDropdown(arg);
        return;
    }
    if (action == "toggle-inspector-section") {
        inspector_.toggleSection(document_, coordinator_, arg);
        return;
    }
    if (action == "toggle-logic-rule-collapsed") {
        logicBoardEditor_.toggleRuleCollapsed(arg);
        return;
    }
    if (action == "collapse-all-logic-rules") {
        logicBoardEditor_.collapseAllRules();
        return;
    }
    if (action == "expand-all-logic-rules") {
        logicBoardEditor_.expandAllRules();
        return;
    }
    if (action == "select-logic-object-type") {
        hideContextMenus();   // then fall through to execute the pick
    } else if (action == "set-logic-key") {
        logicBoardEditor_.closeDropdown();   // then fall through to execute the pick
    }

    if (handleProjectFileAction(action, arg, value)) return;
    if (handleConsoleAction(action, arg, value)) return;
    if (handleAssetsAction(action, arg, value)) return;
    if (handleToolbarAction(action, arg, value)) return;
    if (spriteAnimationEditor_.handleAction(action, arg, value)) return;
    if (tilesetEditor_.handleAction(action, arg, value)) return;
    if (logicBoardEditor_.handleAction(action, arg, value, [this]() {
            hideContextMenus();
            if (document_ && document_->GetContext()) {
                if (Rml::Element* focus = document_->GetContext()->GetFocusElement()) focus->Blur();
            }
        })) return;
    if (handleHierarchyAction(action, arg, value, selected)) return;
    if (handleInspectorAction(action, arg, value, selected)) return;
}

bool EditorUi::handleInspectorAction(const std::string& action, const std::string& arg,
                                     const std::string& value, EntityId selected) {
    if (action == "add-sprite-renderer") {
        addSpriteRenderer(coordinator_);
    } else if (action == "remove-sprite-renderer") {
        removeSpriteRenderer(coordinator_);
    } else if (action == "toggle-sprite-visible") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (inst && inst->spriteRenderer)
            setSpriteRendererVisible(coordinator_, !inst->spriteRenderer->visible);
    } else if (action == "set-sprite-asset") {
        setSpriteRendererAsset(coordinator_, arg);   // arg = assetId ("" clears)
    } else if (action == "set-sprite-animation") {
        // Assign-only: opening the editor for review already has its own
        // dedicated action (open-sprite-animation, the Edit button), so this
        // no longer pops the editor open as a side effect of picking a clip.
        if (!coordinator_.isPlaying()) setSpriteRendererAnimation(coordinator_, arg);
    } else if (action == "commit-animator-speed") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Animator speed is not a number");
        } else if (selected == INVALID_ENTITY) {
            coordinator_.logError("No selected instance");
        } else {
            coordinator_.execute(SetSpriteAnimatorPlaybackSpeedCommand{
                coordinator_.state().activeSceneId, selected, *parsed});
        }
    } else if (action == "toggle-animator-autoplay") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        if (inst && inst->spriteAnimator) {
            coordinator_.execute(SetSpriteAnimatorAutoPlayCommand{
                coordinator_.state().activeSceneId, selected, !inst->spriteAnimator->autoPlay});
        }
    } else if (action == "bring-entity-into-scene") {
        bringSelectedEntityIntoScene(coordinator_);
    } else if (action == "add-box-collider") {
        addBoxCollider(coordinator_);
    } else if (action == "remove-box-collider") {
        removeBoxCollider(coordinator_);
    } else if (action == "toggle-box-enabled") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (inst) {
            const auto& types = coordinator_.document().data().objectTypes;
            const auto typeIt = types.find(inst->objectTypeId);
            if (typeIt != types.end() && typeIt->second.boxCollider2D) {
                const BoxCollider2DComponent& collider = *typeIt->second.boxCollider2D;
                setBoxColliderEnabled(coordinator_, !collider.enabled);
            }
        }
    } else if (action == "set-box-mode") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before editing components");
        } else {
            const std::optional<BoxColliderMode> mode = parseBoxColliderModeArg(arg);
            if (!mode.has_value()) coordinator_.logError("Unknown box collider mode");
            else setBoxColliderMode(coordinator_, *mode);
        }
    } else if (action == "commit-box-offset-x" || action == "commit-box-offset-y"
               || action == "commit-box-size-x" || action == "commit-box-size-y") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (!inst) {
            coordinator_.logError("No selected instance");
        } else {
            const auto& types = coordinator_.document().data().objectTypes;
            const auto typeIt = types.find(inst->objectTypeId);
            if (typeIt == types.end() || !typeIt->second.boxCollider2D) {
                coordinator_.logError("Selected instance has no Box Collider");
            } else {
                const BoxCollider2DComponent& collider = *typeIt->second.boxCollider2D;
                const std::optional<float> parsed = parseNumberField(value);
                if (!parsed.has_value()) {
                    coordinator_.logError("Box Collider value is not a number");
                } else if (action == "commit-box-offset-x") {
                    setBoxColliderOffset(coordinator_, Vec2{*parsed, collider.offset.y});
                } else if (action == "commit-box-offset-y") {
                    setBoxColliderOffset(coordinator_, Vec2{collider.offset.x, *parsed});
                } else if (action == "commit-box-size-x") {
                    setBoxColliderSize(coordinator_, Vec2{*parsed, collider.size.y});
                } else {
                    setBoxColliderSize(coordinator_, Vec2{collider.size.x, *parsed});
                }
            }
        }
    } else if (action == "add-linear-mover") {
        addLinearMover(coordinator_);
    } else if (action == "remove-linear-mover") {
        removeLinearMover(coordinator_);
    } else if (action == "commit-mover-dir-x" || action == "commit-mover-dir-y"
               || action == "commit-mover-speed") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (!inst) {
            coordinator_.logError("No selected instance");
        } else {
            const auto& types = coordinator_.document().data().objectTypes;
            const auto typeIt = types.find(inst->objectTypeId);
            if (typeIt == types.end() || !typeIt->second.linearMover) {
                coordinator_.logError("Selected instance has no Linear Mover");
            } else {
                const LinearMoverComponent& m = *typeIt->second.linearMover;
                const std::optional<float> parsed = parseNumberField(value);
                if (!parsed.has_value()) {
                    coordinator_.logError("Linear Mover value is not a number");
                } else if (action == "commit-mover-dir-x") {
                    setLinearMoverDirection(coordinator_, Vec2{*parsed, m.directionY});
                } else if (action == "commit-mover-dir-y") {
                    setLinearMoverDirection(coordinator_, Vec2{m.directionX, *parsed});
                } else {
                    setLinearMoverSpeed(coordinator_, *parsed);
                }
            }
        }
    } else if (action == "add-top-down") {
        addTopDownController(coordinator_);
    } else if (action == "remove-top-down") {
        removeTopDownController(coordinator_);
    } else if (action == "commit-topdown-speed") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Top Down speed is not a number");
        else setTopDownControllerSpeed(coordinator_, *parsed);
    } else if (action == "add-platformer") {
        addPlatformerController(coordinator_);
    } else if (action == "remove-platformer") {
        removePlatformerController(coordinator_);
    } else if (action == "commit-platformer-move") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Platformer move speed is not a number");
        else setPlatformerMoveSpeed(coordinator_, *parsed);
    } else if (action == "commit-platformer-jump") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Platformer jump speed is not a number");
        else setPlatformerJumpSpeed(coordinator_, *parsed);
    } else if (action == "commit-platformer-gravity") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Platformer gravity is not a number");
        else setPlatformerGravity(coordinator_, *parsed);
    } else if (action == "commit-pos-x") {
        commitInspectorPositionX(coordinator_, selected, value);
    } else if (action == "commit-pos-y") {
        commitInspectorPositionY(coordinator_, selected, value);
    } else if (action == "commit-name") {
        if (selected == INVALID_ENTITY) coordinator_.logError("No selected instance");
        else if (value.empty()) coordinator_.logError("Name cannot be empty");
        else coordinator_.execute(
                RenameEntityCommand{coordinator_.state().activeSceneId, selected, value});
    } else if (action == "commit-type-name") {
        const SceneInstanceDef* inst = (selected != INVALID_ENTITY)
            ? coordinator_.document().findInstanceInScene(coordinator_.state().activeSceneId, selected)
            : nullptr;
        if (!inst) coordinator_.logError("No selected instance");
        else if (value.empty()) coordinator_.logError("Type name cannot be empty");
        else coordinator_.execute(RenameObjectTypeCommand{inst->objectTypeId, value});
    } else if (action == "commit-scene-name") {
        if (value.empty()) coordinator_.logError("Scene name cannot be empty");
        else coordinator_.execute(RenameSceneCommand{coordinator_.state().activeSceneId, value});
    } else if (action == "commit-scene-width" || action == "commit-scene-height") {
        const SceneDef* scene =
            coordinator_.document().findScene(coordinator_.state().activeSceneId);
        const std::optional<float> parsed = parseNumberField(value);
        if (!scene) {
            coordinator_.logError("No selected scene");
        } else if (!parsed.has_value()) {
            coordinator_.logError("Scene size is not a number");
        } else {
            Vec2 size = scene->worldSize;
            if (action == "commit-scene-width") size.x = *parsed;
            else                                size.y = *parsed;
            coordinator_.execute(SetSceneSizeCommand{coordinator_.state().activeSceneId, size});
        }
    } else if (action == "add-tilemap-component") {
        addTilemapComponent(coordinator_);
    } else if (action == "remove-tilemap-component") {
        removeTilemapComponent(coordinator_);
    } else if (action == "set-tilemap-tileset") {
        if (selected != INVALID_ENTITY) {
            coordinator_.execute(
                SetTilemapTilesetCommand{coordinator_.state().activeSceneId, selected, arg});
        }
    } else if (action == "commit-tilemap-cell-width" || action == "commit-tilemap-cell-height") {
        const SceneInstanceDef* inst = (selected != INVALID_ENTITY)
            ? coordinator_.document().findInstanceInScene(coordinator_.state().activeSceneId, selected)
            : nullptr;
        const std::optional<float> parsed = parseNumberField(value);
        if (!inst || !inst->tilemap.has_value()) {
            coordinator_.logError("No selected Tilemap component");
        } else if (!parsed.has_value()) {
            coordinator_.logError("Tilemap cell size is not a number");
        } else {
            Vec2 cellSize = inst->tilemap->cellSize;
            if (action == "commit-tilemap-cell-width") cellSize.x = *parsed;
            else                                       cellSize.y = *parsed;
            coordinator_.execute(
                SetTilemapCellSizeCommand{coordinator_.state().activeSceneId, selected, cellSize});
        }
    } else if (action == "select-tool-select") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Select});
    } else if (action == "select-tool-pan") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Pan});
    } else if (action == "select-tilemap-brush") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Brush});
    } else if (action == "select-tilemap-eraser") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Eraser});
    } else if (action == "select-tilemap-picker") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Picker});
    } else if (action == "select-tilemap-rectangle") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Rectangle});
    } else if (action == "select-tilemap-fill") {
        coordinator_.apply(SetActiveToolIntent{EditorTool::Fill});
    } else if (action == "select-tilemap-rectangle-solid") {
        coordinator_.apply(SetRectangleShapeModeIntent{false});
    } else if (action == "select-tilemap-rectangle-outline") {
        coordinator_.apply(SetRectangleShapeModeIntent{true});
    } else if (action == "select-tilemap-tile") {
        coordinator_.apply(SelectPaintTileIntent{arg});
    } else if (action == "open-tilemap-tileset-editor") {
        // Double-click on a palette thumbnail: no data-arg carries the
        // tileset id (a thumb's own data-arg is its TileId, for the click/
        // select action), so this resolves it from the selected instance's
        // own TilemapComponent instead of overloading one attribute with two
        // different ids.
        if (!coordinator_.isPlaying()) {
            const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
                coordinator_.state().activeSceneId, selected);
            if (inst && inst->tilemap.has_value()) {
                coordinator_.apply(OpenTilesetEditorIntent{inst->tilemap->tilesetAssetId});
            }
        }
    } else {
        return false;
    }
    return true;
}

bool EditorUi::handleToolbarAction(const std::string& action, const std::string& arg,
                                   const std::string& value) {
    if (action == "fit-view-to-bounds") {
        if (fitViewRequest_) fitViewRequest_();   // workspace-only (camera), no command
    } else if (action == "undo") {
        coordinator_.undo();
    } else if (action == "redo") {
        coordinator_.redo();
    } else if (action == "reset-zoom") {
        coordinator_.apply(SetViewportZoomIntent{currentViewSceneId(), 1.0f});   // target unchanged
    } else if (action == "toggle-grid-visible") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before editing the grid");
        } else {
            const SceneId active = coordinator_.state().activeSceneId;
            coordinator_.apply(SetSceneGridVisibilityIntent{
                active, !coordinator_.sceneView(active).gridVisible});
        }
    } else if (action == "toggle-grid-snap") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before editing the grid");
        } else {
            const SceneId active = coordinator_.state().activeSceneId;
            coordinator_.apply(SetSceneGridSnapEnabledIntent{
                active, !coordinator_.sceneView(active).gridSnapEnabled});
        }
    } else if (action == "commit-grid-cell-size") {
        commitGridCellSize(value);
    } else if (action == "set-grid-cell-size") {
        commitGridCellSize(arg);
    } else if (action == "reveal-tilemap-cell-size") {
        const SceneGridPresentation pres = makeSceneGridPresentation(
            coordinator_.document(), coordinator_.state(), coordinator_.state().activeSceneId);
        if (pres.sourceEntityId) {
            coordinator_.apply(RevealInspectorPropertyIntent{
                *pres.sourceEntityId, InspectorProperty::TilemapCellSize});
        }
    } else if (action == "zoom-in" || action == "zoom-out") {
        const SceneId active = currentViewSceneId();
        const float current = coordinator_.sceneView(active).zoom;
        const float factor = (action == "zoom-in") ? 1.2f : (1.0f / 1.2f);
        coordinator_.apply(SetViewportZoomIntent{active, current * factor});
    } else if (action == "play-project") {
        coordinator_.playProject();        // guarded; no-op without a valid start scene
    } else if (action == "play-current-scene") {
        coordinator_.playCurrentScene();   // guarded; no-op without an active scene
    } else if (action == "stop") {
        coordinator_.stopPlaying();
    } else {
        return false;
    }
    return true;
}



bool EditorUi::handleHierarchyAction(const std::string& action, const std::string& arg,
                                     const std::string& value, EntityId selected) {
    if (action == "select-entity") {
        const EntityId entityId = static_cast<EntityId>(std::strtoul(arg.c_str(), nullptr, 10));
        const bool logicWorkspace = coordinator_.state().logicBoardEditor.mode
            == CenterWorkspaceMode::Logic;
        const EditorOperationResult selectedResult = coordinator_.apply(SelectEntityIntent{entityId});
        // Hierarchy navigation in Logic explicitly retargets the board once.
        // No refresh path ever derives the target from SelectionState.
        if (logicWorkspace && selectedResult.ok) {
            if (const SceneInstanceDef* instance = coordinator_.document().findInstanceInScene(
                    coordinator_.state().activeSceneId, entityId)) {
                coordinator_.apply(OpenLogicBoardIntent{instance->objectTypeId});
            }
        }
    } else if (action == "select-scene") {
        coordinator_.apply(SelectSceneIntent{arg});
    } else if (action == "add-scene") {
        addScene(coordinator_);
    } else if (action == "delete-scene") {
        // No arg → the active scene; the coordinator reconciles the workspace.
        hideContextMenus();
        deleteScene(coordinator_, arg.empty() ? coordinator_.state().activeSceneId : arg);
    } else if (action == "add-entity") {
        if (addEntityRequest_) addEntityRequest_();
        else addEntity(coordinator_);
    } else if (action == "add-instance") {
        hideContextMenus();
        if (addInstanceRequest_) addInstanceRequest_();
        else addInstanceOfSelectedType(coordinator_);
    } else if (action == "add-instance-of-type") {
        hideContextMenus();
        addInstanceOfType(coordinator_, arg);
    } else if (action == "select-layer") {
        coordinator_.apply(SetActiveLayerIntent{coordinator_.state().activeSceneId, arg});
    } else if (action == "toggle-layer-visible") {
        coordinator_.apply(
            ToggleLayerEditorVisibilityIntent{coordinator_.state().activeSceneId, arg});
    } else if (action == "toggle-layer-locked") {
        const SceneId active = coordinator_.state().activeSceneId;
        coordinator_.execute(
            SetLayerLockedCommand{active, arg, !coordinator_.document().isLayerLocked(active, arg)});
    } else if (action == "begin-layer-rename") {
        inspector_.beginSceneLayerRename(document_, coordinator_, arg);
    } else if (action == "commit-layer-rename") {
        inspector_.commitSceneLayerRename(document_, coordinator_, value);
    } else if (action == "cancel-layer-rename") {
        inspector_.cancelSceneLayerRename(document_, coordinator_);
    } else if (action == "add-layer") {
        const SceneId active = coordinator_.state().activeSceneId;
        const SceneDef* scene = coordinator_.document().findScene(active);
        if (scene) {
            int n = 1;
            std::string id;
            std::string name;
            do {
                id = "layer-" + std::to_string(n);
                name = "Layer " + std::to_string(n);
                ++n;
            } while (coordinator_.document().hasLayer(active, id)
                     || sceneLayerNameExists(*scene, name));
            coordinator_.execute(AddSceneLayerCommand{
                active, id, name, scene->layers.size()});
        }
    } else if (action == "move-layer-up" || action == "move-layer-down") {
        const SceneId active = coordinator_.state().activeSceneId;
        const SceneDef* scene = coordinator_.document().findScene(active);
        if (scene) {
            std::size_t i = scene->layers.size();
            for (std::size_t k = 0; k < scene->layers.size(); ++k)
                if (scene->layers[k].id == arg) { i = k; break; }
            if (i < scene->layers.size()) {
                // "up" in the panel = toward foreground = a higher vector index.
                if (action == "move-layer-up")
                    coordinator_.execute(MoveSceneLayerCommand{active, arg, i + 1});
                else if (i > 0)
                    coordinator_.execute(MoveSceneLayerCommand{active, arg, i - 1});
            }
        }
    } else if (action == "remove-layer") {
        coordinator_.execute(RemoveSceneLayerCommand{coordinator_.state().activeSceneId, arg});
    } else if (action == "set-entity-layer") {
        if (selected != INVALID_ENTITY)
            coordinator_.execute(
                SetEntityLayerCommand{coordinator_.state().activeSceneId, selected, arg});
    } else if (action == "create-entity-here") {
        hideContextMenus();
        if (createEntityHereRequest_) createEntityHereRequest_();
    } else if (action == "create-instance-here") {
        hideContextMenus();
        if (createInstanceHereRequest_) createInstanceHereRequest_();
    } else if (action == "clone-entity") {
        hideContextMenus();
        cloneSelectedEntity(coordinator_);
    } else if (action == "delete-entity") {
        hideContextMenus();
        deleteSelectedEntity(coordinator_);
    } else if (action == "set-start-scene") {
        hideContextMenus();
        setStartScene(coordinator_, arg.empty() ? coordinator_.state().activeSceneId : arg);
    } else {
        return false;
    }
    return true;
}

bool EditorUi::handleAssetsAction(const std::string& action, const std::string& arg,
                                  const std::string& value) {
    if (action == "set-asset-filter") {
        coordinator_.apply(SetAssetFilterIntent{value});
    } else if (action == "import-image") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before importing assets");
            return true;
        }
        if (importAssetRequest_) importAssetRequest_(AssetKind::Image);
    } else if (action == "import-audio") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before importing assets");
            return true;
        }
        if (importAssetRequest_) importAssetRequest_(AssetKind::Audio);
    } else if (action == "import-font") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before importing assets");
            return true;
        }
        if (importAssetRequest_) importAssetRequest_(AssetKind::Font);
    } else if (action == "remove-image-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveImageAssetCommand{arg});
    } else if (action == "remove-audio-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveAudioAssetCommand{arg});
    } else if (action == "remove-font-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveFontAssetCommand{arg});
    } else {
        return false;
    }
    return true;
}

bool EditorUi::handleConsoleAction(const std::string& action, const std::string& arg,
                                   const std::string& value) {
    if (action == "select-console") {
        console_.select(static_cast<std::size_t>(std::strtoul(arg.c_str(), nullptr, 10)),
                        document_, coordinator_);
    } else if (action == "copy-console") {
        copySelectedConsoleMessage();
    } else if (action == "clear-console") {
        coordinator_.clearConsole();
    } else if (action == "toggle-console") {
        coordinator_.apply(ToggleConsoleIntent{});
    } else if (action == "toggle-console-info") {
        coordinator_.apply(SetConsoleShowInfoIntent{!coordinator_.uiState().consoleShowInfo});
    } else if (action == "toggle-console-warning") {
        coordinator_.apply(SetConsoleShowWarningIntent{!coordinator_.uiState().consoleShowWarning});
    } else if (action == "toggle-console-error") {
        coordinator_.apply(SetConsoleShowErrorIntent{!coordinator_.uiState().consoleShowError});
    } else if (action == "commit-console-filter") {
        coordinator_.apply(SetConsoleFilterIntent{value});
    } else {
        return false;
    }
    return true;
}

bool EditorUi::handleProjectFileAction(const std::string& action, const std::string& arg,
                                       const std::string& value) {
    (void)arg;
    if (action == "commit-project-name") {
        if (!value.empty()) coordinator_.execute(RenameProjectCommand{value});
    } else if (action == "new-project") {
        if (newProjectRequest_) newProjectRequest_();
    } else if (action == "open-project") {
        if (openProjectRequest_) openProjectRequest_();
    } else if (action == "save-project") {
        if (saveProjectRequest_) saveProjectRequest_();
    } else if (action == "save-project-as") {
        if (saveProjectAsRequest_) saveProjectAsRequest_();
    } else {
        return false;
    }
    return true;
}

void EditorUi::handleDrag(const std::string& action, float mouseX, float mouseY) {
    if (!document_) return;
    Rml::Context* ctx = document_->GetContext();
    const Rml::Vector2i dims = ctx ? ctx->GetDimensions() : Rml::Vector2i(0, 0);

    const auto px = [](float v) { return std::to_string(static_cast<int>(v)) + "px"; };

    if (action == "resize-left") {
        coordinator_.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, mouseX});
        if (Rml::Element* el = document_->GetElementById("left-col"))
            el->SetProperty("width", px(coordinator_.uiState().leftPanelWidth));
        logicBoardEditor_.syncResponsiveClass();
    } else if (action == "resize-right") {
        coordinator_.apply(ResizePanelIntent{ResizePanelIntent::Panel::Right,
                                             static_cast<float>(dims.x) - mouseX});
        if (Rml::Element* el = document_->GetElementById("right-col"))
            el->SetProperty("width", px(coordinator_.uiState().rightPanelWidth));
        logicBoardEditor_.syncResponsiveClass();
    } else if (action == "resize-console") {
        coordinator_.apply(ResizePanelIntent{ResizePanelIntent::Panel::Console,
                                             static_cast<float>(dims.y) - mouseY});
        if (Rml::Element* el = document_->GetElementById("console"))
            el->SetProperty("height", px(coordinator_.uiState().consoleHeight));
    }
}

} // namespace ArtCade::EditorNative
