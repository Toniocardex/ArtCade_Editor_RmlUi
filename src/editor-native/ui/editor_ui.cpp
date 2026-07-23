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
#include "editor-native/commands/generated_sfx_macros.h"
#include "editor-native/model/generated_sfx_policy.h"
#include "editor-native/model/generated_sfx_preset_catalog.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/script_asset_commands.h"
#include "editor-native/commands/script_attachment_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/project_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/tile_palette_availability.h"
#include "editor-native/model/tile_palette_projection.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/app/scene_view_interaction.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view_camera.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>

#include <raylib.h>   // SetClipboardText (Console copy)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace ArtCade::EditorNative {

std::optional<AssetMenuKind> parseAssetMenuKind(const std::string& tag) {
    if (tag == "image")   return AssetMenuKind::Image;
    if (tag == "anim")    return AssetMenuKind::Animation;
    if (tag == "tileset") return AssetMenuKind::Tileset;
    if (tag == "sfx")     return AssetMenuKind::GeneratedSfx;
    if (tag == "audio")   return AssetMenuKind::Audio;
    if (tag == "font")    return AssetMenuKind::Font;
    if (tag == "script")  return AssetMenuKind::Script;
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
    if (const auto* generatedSfx = findGeneratedSfxEditorAction(action))
        return generatedSfx->requiresPendingEditResolution;
    static constexpr std::string_view actions[] = {
        "new-project", "open-project", "save-project", "save-project-as",
        "play-project", "play-current-scene",
        "select-entity", "select-scene", "select-layer", "select-animation-clip",
        "open-sprite-animation", "close-sprite-animation",
        "open-tileset-editor", "close-tileset-editor",
        "open-scene-workspace", "open-logic-workspace", "open-script-workspace",
        "open-script", "activate-script", "close-script",
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
        if (action == "edit-script-buffer") {
            if (type == "focus") { ui_.setScriptEditorFocused(true); return; }
            if (type == "blur") { ui_.setScriptEditorFocused(false); return; }
            if (type == "change") {
                ui_.handleScriptTextChanged(formValue(actionElement, event));
                return;
            }
            if (type == "click") { ui_.handleScriptCursorChanged(); return; }
            if (type == "keyup" || type == "scroll") {
                ui_.handleScriptCursorChanged();
                return;
            }
            if (type == "keydown") {
                const int key = event.GetParameter<int>("key_identifier", 0);
                const bool control = event.GetParameter<int>("ctrl_key", 0) != 0;
                const bool shift = event.GetParameter<int>("shift_key", 0) != 0;
                const bool alt = event.GetParameter<int>("alt_key", 0) != 0;
                const bool intercept = key == Rml::Input::KI_TAB
                    || key == Rml::Input::KI_RETURN
                    || key == Rml::Input::KI_NUMPADENTER
                    || key == Rml::Input::KI_ESCAPE
                    || (control && (key == Rml::Input::KI_S
                        || key == Rml::Input::KI_F
                        || key == Rml::Input::KI_Z
                        || key == Rml::Input::KI_Y
                        || key == Rml::Input::KI_M
                        || key == Rml::Input::KI_SPACE
                        || key == Rml::Input::KI_OEM_2))
                    || (control && shift && key == Rml::Input::KI_L)
                    || (alt && (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN));
                if (intercept) {
                    ui_.handleScriptEditorShortcut(key, control, shift, alt);
                    event.StopImmediatePropagation();
                    return;
                }
                ui_.handleScriptCursorChanged();
                return;
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

        // ADR-0004: Search Key is live UI state. Escape closes the search;
        // RmlUi's `change` event carries typed and pasted text after its
        // input buffer has updated, then the Logic panel redraws filtered
        // supported-key results.
        if (action == "filter-logic-key-search") {
            if (type == "keydown") {
                const int key = event.GetParameter<int>("key_identifier", 0);
                if (key == Rml::Input::KI_ESCAPE) {
                    ui_.handleAction("toggle-logic-key-search", arg, {});
                    event.StopPropagation();
                }
                return;
            }
            if (type == "change") {
                ui_.handleAction(action, arg, formValue(actionElement, event));
            }
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

        // Create-from-current name field: Enter confirms; blur only validates;
        // Escape cancels. Must not share the generic commit- blur/Enter path.
        const auto* generatedSfxAction = findGeneratedSfxEditorAction(action);
        if (generatedSfxAction
            && generatedSfxAction->action
                == GeneratedSfxEditorAction::EditCreateFromCurrentName) {
            const int key = event.GetParameter<int>("key_identifier", 0);
            if (type == "keydown" && key == Rml::Input::KI_ESCAPE) {
                ui_.closeSfxCreateFromCurrentDialog();
                event.StopPropagation();
                return;
            }
            const bool enter = type == "keydown"
                && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER);
            if (type == "blur") {
                ui_.validateSfxCreateFromCurrentName(formValue(actionElement, event));
                return;
            }
            if (enter) {
                ui_.confirmSfxCreateFromCurrent(formValue(actionElement, event));
                event.StopPropagation();
                return;
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
        // Generated SFX macro range-input: dragstart/dragend bracket a real
        // drag gesture (RmlUi only fires them when the mouse actually moves
        // while held; a plain click or keyboard nudge never sees either), so
        // a single commit lands on dragend instead of once per "change" tick.
        if (generatedSfxAction
            && generatedSfxAction->action == GeneratedSfxEditorAction::DragMacro) {
            if (type == "dragstart") { ui_.beginSfxMacroDrag(arg); return; }
            if (type == "dragend") { ui_.commitSfxMacroDrag(); return; }
            if (type == "change") {
                ui_.handleSfxMacroChange(arg, event.GetParameter<float>("value", 0.f));
                return;
            }
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
      logicBoardEditor_(coordinator, document),
      scriptEditor_(coordinator, document),
      generatedSfxEditor_(coordinator) {}

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
        doc->AddEventListener("keyup", listener_.get());
        doc->AddEventListener("scroll", listener_.get());
        doc->AddEventListener("change", listener_.get());
        doc->AddEventListener("drag", listener_.get());
        doc->AddEventListener("dragstart", listener_.get());
        doc->AddEventListener("dragend", listener_.get());
    };
    bindDocument(document_);
    bindDocument(animationDocument_);
    bindDocument(tilesetDocument_);

    // Initial full paint of every panel.
    coordinator_.consumeInvalidations();
    applyInvalidations(EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                       | EditorInvalidation::Console  | EditorInvalidation::Toolbar
                       | EditorInvalidation::Assets   | EditorInvalidation::Layout
                       | EditorInvalidation::LogicBoard | EditorInvalidation::ScriptEditor);
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
            doc->RemoveEventListener("keyup", listener_.get());
            doc->RemoveEventListener("scroll", listener_.get());
            doc->RemoveEventListener("change", listener_.get());
            doc->RemoveEventListener("drag", listener_.get());
            doc->RemoveEventListener("dragstart", listener_.get());
            doc->RemoveEventListener("dragend", listener_.get());
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
    createScriptRequest_ = {};
    removeScriptRequest_ = {};
    openScriptRequest_ = {};
    saveScriptRequest_ = {};
    saveAllScriptsRequest_ = {};
    closeScriptRequest_ = {};
    restartScriptsRequest_ = {};
    addEntityRequest_ = {};
    addInstanceRequest_ = {};
    createEntityHereRequest_ = {};
    createInstanceHereRequest_ = {};
    fitViewRequest_ = {};
    generatedSfxEditor_.detach();
    generatedSfxRefreshPending_ = false;
    spriteAnimationEditor_.detach();
    tilesetEditor_.detach();
    logicBoardEditor_.detach();
    scriptEditor_.detach();

    // These are non-owning and become invalid as soon as the host unloads its
    // documents. Null them even when bind never succeeded (missing document).
    document_ = nullptr;
    animationDocument_ = nullptr;
    tilesetDocument_ = nullptr;
}

void EditorUi::processFrame() {
    if (!listener_ || !document_) return;
    scriptEditor_.processFrame();
    applyInvalidations(coordinator_.consumeInvalidations());
    if (generatedSfxRefreshPending_) {
        generatedSfxRefreshPending_ = false;
        refreshGeneratedSfxEditor();
    }
    // Deferred context menus: shown here, after the application's
    // outside-click check for this frame has already run.
    showPendingHierarchyMenu();
    showPendingAssetMenu();
    // The preview playhead advances without invalidation (workspace tick), so
    // the timeline highlight and Play/Pause affordance follow it live here,
    // class-only — never via a markup rebuild that would steal input focus.
    spriteAnimationEditor_.updatePlayhead();
}

void EditorUi::restoreAfterRmlLayout() {
    if (!listener_ || !document_) return;
    logicBoardEditor_.restoreAfterLayout();
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
        tilePaletteDock_.refresh(document_, coordinator_);
    }
    if (has(flags, EditorInvalidation::Console))
        console_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Assets) || has(flags, EditorInvalidation::Project))
        assets_.refresh(document_, coordinator_, [&](const std::string& id) {
            return generatedSfxEditor_.status(id);
        });
    if (has(flags, EditorInvalidation::Assets) || has(flags, EditorInvalidation::Project)
        || has(flags, EditorInvalidation::Toolbar)) {
        generatedSfxRefreshPending_ = false;
        refreshGeneratedSfxEditor();
    }
    if (has(flags, EditorInvalidation::LogicBoard) || has(flags, EditorInvalidation::Project))
        logicBoardEditor_.refresh();
    if (has(flags, EditorInvalidation::ScriptEditor) || has(flags, EditorInvalidation::Project))
        scriptEditor_.refresh();
    if (has(flags, EditorInvalidation::Viewport) || has(flags, EditorInvalidation::Assets)
        || has(flags, EditorInvalidation::Project)) {
        spriteAnimationEditor_.refresh();
        tilesetEditor_.refresh();
    }
    if (has(flags, EditorInvalidation::Toolbar) || has(flags, EditorInvalidation::Assets)
        || has(flags, EditorInvalidation::Inspector))
        refreshToolbar();
    if (has(flags, EditorInvalidation::Toolbar) || has(flags, EditorInvalidation::Project)
        || has(flags, EditorInvalidation::LogicBoard)
        || has(flags, EditorInvalidation::ScriptEditor))
        refreshStatusBar();
    if (has(flags, EditorInvalidation::Viewport)) {
        updateZoomReadout();
        tilesetEditor_.updateZoomReadout();
    }
    if (has(flags, EditorInvalidation::Layout))
        refreshLayout();
    if (has(flags, EditorInvalidation::LogicBoard) || has(flags, EditorInvalidation::Viewport)
        || has(flags, EditorInvalidation::Project)
        || has(flags, EditorInvalidation::ScriptEditor)) {
        refreshCenterWorkspace();
    }
    // Tile Palette dock: full refresh on Inspector (stamp/zoom/grid/selection).
    // Also sync when Scene↔Logic/Script or Project changes without Inspector —
    // never on bare Viewport/Layout (pan, zoom, splitter) which would
    // SetInnerRML-rebuild #tile-palette every frame.
    if (!has(flags, EditorInvalidation::Inspector)
        && (has(flags, EditorInvalidation::LogicBoard)
            || has(flags, EditorInvalidation::ScriptEditor)
            || has(flags, EditorInvalidation::Project))) {
        tilePaletteDock_.refresh(document_, coordinator_);
    }
}

void EditorUi::refreshCenterWorkspace() {
    if (!document_) return;
    const CenterWorkspaceMode mode = coordinator_.state().centerWorkspaceMode;
    const bool scene = mode == CenterWorkspaceMode::Scene;
    const bool logic = mode == CenterWorkspaceMode::Logic;
    const bool script = mode == CenterWorkspaceMode::Script;
    if (Rml::Element* el = document_->GetElementById("viewport")) el->SetClass("hidden", !scene);
    if (Rml::Element* el = document_->GetElementById("logic-board-panel")) el->SetClass("hidden", !logic);
    if (Rml::Element* el = document_->GetElementById("script-editor-panel")) el->SetClass("hidden", !script);
    if (Rml::Element* el = document_->GetElementById("center-tab-scene")) el->SetClass("active", scene);
    if (Rml::Element* el = document_->GetElementById("center-tab-logic")) el->SetClass("active", logic);
    if (Rml::Element* el = document_->GetElementById("center-tab-script")) el->SetClass("active", script);
    if (Rml::Element* el = document_->GetElementById("scene-context-tools")) el->SetClass("hidden", !scene);
    if (Rml::Element* el = document_->GetElementById("logic-context-tools")) el->SetClass("hidden", !logic);
    if (Rml::Element* el = document_->GetElementById("script-context-tools")) el->SetClass("hidden", !script);
    // Rules are edited inline in the board. Keeping the entity Inspector visible
    // would present a second, unrelated target (instance vs owning Object Type).
    // Its inline width remains untouched and is restored automatically in Scene.
    if (Rml::Element* el = document_->GetElementById("split-right")) el->SetClass("hidden", !scene);
    if (Rml::Element* el = document_->GetElementById("right-col")) el->SetClass("hidden", !scene);
}

void EditorUi::refreshLayout() {
    if (!document_) return;
    const bool consoleVisible = coordinator_.uiState().consoleVisible;
    if (Rml::Element* console = document_->GetElementById("console"))
        console->SetClass("hidden", !consoleVisible);
    if (Rml::Element* splitter = document_->GetElementById("split-console"))
        splitter->SetClass("hidden", !consoleVisible);
    if (Rml::Element* dock = document_->GetElementById("tile-palette-dock")) {
        if (!dock->IsClassSet("hidden")) {
            dock->SetProperty(
                "height",
                std::to_string(static_cast<int>(coordinator_.uiState().tilePaletteDockHeight))
                    + "px");
        }
    }
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

void EditorUi::setPlayHandlers(ProjectFileRequest playProject,
                               ProjectFileRequest playCurrentScene) {
    playProjectRequest_ = std::move(playProject);
    playCurrentSceneRequest_ = std::move(playCurrentScene);
}

void EditorUi::setImportHandler(ImportAssetRequest importAsset) {
    importAssetRequest_ = std::move(importAsset);
}

void EditorUi::setCreateScriptHandler(ProjectFileRequest createScript) {
    createScriptRequest_ = std::move(createScript);
}

void EditorUi::setRemoveScriptHandler(ScriptAssetRequest removeScript) {
    removeScriptRequest_ = std::move(removeScript);
}

void EditorUi::setScriptEditorHandlers(ScriptAssetRequest openScript,
                                       ScriptAssetRequest saveScript,
                                       ProjectFileRequest saveAllScripts,
                                       ScriptCloseRequest closeScript,
                                       ProjectFileRequest restartAndApplyScripts) {
    openScriptRequest_ = std::move(openScript);
    saveScriptRequest_ = std::move(saveScript);
    saveAllScriptsRequest_ = std::move(saveAllScripts);
    closeScriptRequest_ = std::move(closeScript);
    restartScriptsRequest_ = std::move(restartAndApplyScripts);
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

void EditorUi::setGeneratedSfxHandlers(GeneratedSfxRequest preview,
                                       WorkspaceRequest stopPreview,
                                       GeneratedSfxRequest generate) {
    generatedSfxEditor_.setGenerationHandlers(
        std::move(preview), std::move(stopPreview), std::move(generate));
}

void EditorUi::setGeneratedSfxDiagnosticHandler(
    GeneratedSfxRequest dismissDiagnostic) {
    generatedSfxEditor_.setDiagnosticHandler(std::move(dismissDiagnostic));
}

void EditorUi::setGeneratedSfxCreateFromCurrentHandler(
    GeneratedSfxCreateFromCurrentRequest request) {
    generatedSfxEditor_.setCreateFromCurrentHandler(std::move(request));
}

void EditorUi::setGeneratedSfxDeleteHandler(
    GeneratedSfxDeleteRequest request) {
    generatedSfxEditor_.setDeleteHandler(std::move(request));
}

void EditorUi::setSfxBatchHandlers(WorkspaceRequest regenerateAllStale,
                                   WorkspaceRequest cancelBatch,
                                   WorkspaceRequest dismissSummary) {
    generatedSfxEditor_.setBatchHandlers(
        std::move(regenerateAllStale), std::move(cancelBatch),
        std::move(dismissSummary));
}

void EditorUi::setSfxBatchState(SfxBatchState state) {
    const bool changed = generatedSfxEditor_.setBatchState(std::move(state));
    if (changed && generatedSfxEditor_.viewModel().workspaceOpen)
        refreshGeneratedSfxEditor();
}

void EditorUi::setProjectSavedQuery(ProjectSavedQuery query) {
    generatedSfxEditor_.setProjectSavedQuery(std::move(query));
}

void EditorUi::setGeneratedSfxGenerationAvailabilityQuery(
    GeneratedSfxGenerationAvailabilityQuery query) {
    generatedSfxEditor_.setGenerationAvailabilityQuery(std::move(query));
}

void EditorUi::setGeneratedSfxStatusQuery(GeneratedSfxStatusQuery query) {
    generatedSfxEditor_.setStatusQuery(std::move(query));
}

void EditorUi::notifyGeneratedSfxOutputReady(const std::string& id) {
    generatedSfxEditor_.notifyOutputReady(id);
    refreshGeneratedSfxEditor();
}

void EditorUi::notifyGeneratedSfxStatusChanged() {
    if (generatedSfxEditor_.viewModel().workspaceOpen) refreshGeneratedSfxEditor();
}

void EditorUi::deferGeneratedSfxRefresh() {
    generatedSfxRefreshPending_ = true;
}

void EditorUi::closeSfxCreateFromCurrentDialog() {
    if (generatedSfxEditor_.closeCreateFromCurrentDialog().deferRefresh)
        deferGeneratedSfxRefresh();
}

void EditorUi::validateSfxCreateFromCurrentName(const std::string& value) {
    generatedSfxEditor_.validateCreateFromCurrentName(value);
    // A real pointer click blurs the name field on press and emits click only
    // on release, potentially in the next frame. Any rebuild here can destroy
    // the pressed CTA before click is emitted. Blur therefore updates only the
    // controller state; Enter/CTA performs and refreshes the visible validation.
}

void EditorUi::confirmSfxCreateFromCurrent(const std::string& value) {
    const auto update = generatedSfxEditor_.confirmCreateFromCurrent(value);
    if (update.deferRefresh) deferGeneratedSfxRefresh();
    else if (update.refresh) refreshGeneratedSfxEditor();
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
                sourceImageId = asset.sourceImageAssetId;
                break;
            }
            break;
        case AssetMenuKind::Tileset:
            if (const TilesetAsset* ts = coordinator_.document().findTilesetAsset(request.assetId)) {
                exists = true;
                sourceImageId = ts->imageAssetId;
            }
            break;
        case AssetMenuKind::GeneratedSfx:
            exists = coordinator_.document().hasGeneratedSfx(request.assetId);
            break;
        case AssetMenuKind::Audio:
            for (const AudioAssetDef& asset : doc.audioAssets)
                if (asset.assetId == request.assetId) { exists = true; break; }
            break;
        case AssetMenuKind::Font:
            for (const FontAssetDef& asset : doc.fontAssets)
                if (asset.assetId == request.assetId) { exists = true; break; }
            break;
        case AssetMenuKind::Script:
            exists = coordinator_.document().hasScriptAsset(request.assetId);
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
    const bool isGeneratedSfx = request.kind == AssetMenuKind::GeneratedSfx;
    setEntry("actx-edit", isAnim || isTileset || isGeneratedSfx,
             isAnim ? "open-sprite-animation"
                    : isTileset ? "open-tileset-editor" : "open-generated-sfx",
             request.assetId);
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
      : isGeneratedSfx ? "remove-generated-sfx"
      : request.kind == AssetMenuKind::Audio ? "remove-audio-asset"
      : request.kind == AssetMenuKind::Font ? "remove-font-asset"
                                            : "remove-script-asset";
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
    if (Rml::Element* menu = document_->GetElementById("logic-more-menu")) {
        menu->SetClass("hidden", true);
    }
    if (Rml::Element* trigger = document_->GetElementById("logic-more-trigger")) {
        trigger->SetClass("open", false);
    }
    viewportContextMenuVisible_ = false;
    hierarchyContextMenuVisible_ = false;
    assetsContextMenuVisible_ = false;
    logicTypeMenuVisible_ = false;
    logicMoreMenuVisible_ = false;
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
        || hits("logic-type-menu", logicTypeMenuVisible_)
        || hits("logic-more-menu", logicMoreMenuVisible_);
}

std::string sfxInput(const char* label, const char* field, const std::string& value);
std::string sfxInput(const char* label, const char* field, float value);
std::string sfxToggle(const char* label, const char* field, bool enabled);
std::string sfxPitchFields(const artcade::sfx::PitchParams& pitch,
                           const std::string& prefix);
// Primary Voice's Advanced section uses this plus sfxPitchFields in
// sequence -- equivalent to the old sfxVoiceSection, just decomposed so
// Secondary Voice's compact view can render Mix/Detune/Copy once and the
// "More settings" sub-section can render oscillator + pitch fields without
// repeating them (the exact duplication a single shared sfxVoiceSection
// risked between "compact" and "more").
std::string sfxVoiceOscillatorFields(const artcade::sfx::VoiceParams& voice, const char* prefix);
std::string sfxSecondaryCompactFields(const artcade::sfx::VoiceParams& voice);
// Collapsible Advanced-section header: caret (data-action="toggle-sfx-
// section") + title, and for layers that can be switched off, an inline
// Enabled toggle so on/off stays visible even while the body is collapsed.
std::string sfxSectionHeader(const char* title, const std::string& sectionId, bool collapsed,
                             const char* toggleField, bool toggleOn);
// One Simple-mode macro row: a native <input type="range"> (drag/click/
// keyboard all handled by RmlUi's WidgetSlider) paired with a plain numeric
// text input showing the same value in real-world display units, plus a
// unit label. See generated_sfx_macros.h for the slider-space/display-space
// distinction (only Pitch's Hz differs from its [0,1] log-mapped slider).
std::string sfxMacroRow(const SfxMacro& macro, const artcade::sfx::SfxRecipe& recipe);
std::string formatSfxMacroNumber(const SfxMacro& macro, float displayValue);
// Empty when the recipe has been hand-edited since the last preset apply --
// never claim a preset is still "active" once the user has touched a macro,
// even if a coincidental value match would technically pass the comparison.

const char* generatedSfxStatusClass(GeneratedSfxObservedStatus status) {
    switch (status) {
    case GeneratedSfxObservedStatus::UpToDate:
    case GeneratedSfxObservedStatus::Generating: return "ready";
    case GeneratedSfxObservedStatus::RecipeModified:
    case GeneratedSfxObservedStatus::MissingOutput:
    case GeneratedSfxObservedStatus::GenerationFailed:
    case GeneratedSfxObservedStatus::Collision: return "stale";
    }
    return "";
}

bool matchesSfxBrowserFilter(const std::string& filter,
                             const artcade::sfx::GeneratedSfxDef& definition) {
    if (filter.empty()) return true;
    std::string haystack = definition.name + " " + definition.id + " " + definition.outputPath;
    std::string needle = filter;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}

void EditorUi::refreshGeneratedSfxEditor() {
    if (!document_) return;
    generatedSfxEditor_.reconcileDocument();
    const GeneratedSfxEditorViewModel sfxView = generatedSfxEditor_.viewModel();
    Rml::Element* root = document_->GetElementById("sfx-editor");
    Rml::Element* browser = document_->GetElementById("sfx-editor-browser");
    Rml::Element* body = document_->GetElementById("sfx-editor-body");
    if (!root || !browser || !body) return;
    if (!sfxView.visible) {
        root->SetClass("hidden", true);
        return;
    }
    const artcade::sfx::GeneratedSfxDef* definition = sfxView.selectedId
        ? coordinator_.document().findGeneratedSfx(*sfxView.selectedId) : nullptr;
    root->SetClass("hidden", false);
    root->SetClass("empty", definition == nullptr);
    if (Rml::Element* title = document_->GetElementById("sfx-editor-title"))
        title->SetInnerRML(definition ? escapeRml(definition->name)
                                      : "Generated SFX");

    // Persistent library browser — projection of ProjectDocument.generatedSfx.
    std::size_t staleCount = 0;
    for (const artcade::sfx::GeneratedSfxDef& entry :
         coordinator_.document().data().generatedSfx) {
        if (generatedSfxEditor_.status(entry.id).status
            == GeneratedSfxObservedStatus::RecipeModified) {
            ++staleCount;
        }
    }
    std::string browserHtml =
        "<div class=\"sfx-browser-toolbar\">"
        "<input class=\"sfx-browser-search\" type=\"text\" data-action=\"commit-sfx-browser-search\" "
        "placeholder=\"Search…\" value=\"" + escapeRml(sfxView.browserFilter) + "\"/>"
        "<button class=\"sfx-browser-add\" data-action=\"toggle-sfx-create-menu\" "
        "title=\"New Generated SFX\">+</button></div>";
    if (sfxView.batch.active) {
        std::size_t done = 0;
        for (const SfxBatchItem& item : sfxView.batch.items) {
            if (item.status != SfxBatchItemStatus::Pending
                && item.status != SfxBatchItemStatus::Generating) {
                ++done;
            }
        }
        const std::size_t total = sfxView.batch.items.size();
        const std::size_t displayIndex = std::min(done + 1, total);
        const int percent = total == 0 ? 0
            : static_cast<int>((done * 100) / total);
        std::string currentName;
        if (sfxView.batch.currentIndex < sfxView.batch.items.size()) {
            const std::string& id = sfxView.batch.items[sfxView.batch.currentIndex].id;
            if (const auto* def = coordinator_.document().findGeneratedSfx(id))
                currentName = def->name;
            else
                currentName = id;
        }
        browserHtml +=
            "<div class=\"sfx-batch-panel\">"
            "<div class=\"sfx-batch-title\">Generating "
            + std::to_string(displayIndex) + " of " + std::to_string(total) + "</div>"
            "<div class=\"sfx-batch-name\">" + escapeRml(currentName) + "</div>"
            "<div class=\"sfx-batch-bar\"><div class=\"sfx-batch-bar-fill\" style=\"width:"
            + std::to_string(percent) + "%;\"></div></div>"
            "<button class=\"sfx-batch-cancel\" data-action=\"cancel-sfx-batch\" "
            "title=\"Finish the current item, then stop\">"
            + std::string(sfxView.batch.cancelRequested
                              ? "Stopping after current…"
                              : "Cancel Queue")
            + "</button></div>";
    } else if (sfxView.batch.summaryVisible) {
        browserHtml +=
            "<div class=\"sfx-batch-panel\">"
            "<div class=\"sfx-batch-title\">Regeneration complete</div>"
            "<div class=\"sfx-batch-summary\">"
            + std::to_string(sfxView.batch.succeeded) + " succeeded<br/>"
            + std::to_string(sfxView.batch.skipped) + " skipped<br/>"
            + std::to_string(sfxView.batch.failed) + " failed<br/>"
            + std::to_string(sfxView.batch.cancelled) + " cancelled"
            + "</div>"
            "<button class=\"sfx-batch-cancel\" data-action=\"dismiss-sfx-batch-summary\">Close</button>"
            "</div>";
    } else if (staleCount > 0 && generatedSfxEditor_.projectSaved()) {
        browserHtml +=
            "<button class=\"sfx-batch-cta\" data-action=\"regenerate-all-stale-sfx\">"
            "Regenerate " + std::to_string(staleCount)
            + (staleCount == 1 ? " Stale" : " Stale") + "</button>";
    }
    if (sfxView.createMenuOpen) {
        browserHtml +=
            "<div class=\"sfx-browser-create-menu\">"
            "<div class=\"sfx-browser-create-title\">New Generated SFX</div>";
        for (const auto& preset : generatedSfxPresetCatalog()) {
            browserHtml +=
                "<button class=\"sfx-browser-create-entry\" "
                "data-action=\"create-generated-sfx\" data-arg=\""
                + std::string(preset.id) + "\">" + std::string(preset.label)
                + "</button>";
        }
        browserHtml += "</div>";
    }
    browserHtml += "<div class=\"sfx-browser-list\">";
    std::size_t shown = 0;
    for (const artcade::sfx::GeneratedSfxDef& entry :
         coordinator_.document().data().generatedSfx) {
        if (!matchesSfxBrowserFilter(sfxView.browserFilter, entry)) continue;
        ++shown;
        const bool selected = definition && entry.id == definition->id;
        const GeneratedSfxStatusProjection entryStatus =
            generatedSfxEditor_.status(entry.id);
        const char* status = generatedSfxObservedStatusLabel(entryStatus.status);
        const char* statusClass = generatedSfxStatusClass(entryStatus.status);
        browserHtml += "<div class=\"sfx-browser-row";
        if (selected) browserHtml += " selected";
        browserHtml += "\">"
            "<button class=\"sfx-browser-row-main\" data-action=\"open-generated-sfx\" data-arg=\""
            + escapeRml(entry.id) + "\">"
            "<span class=\"sfx-browser-name\">" + escapeRml(entry.name) + "</span>"
            "<span class=\"sfx-browser-status";
        if (statusClass[0] != '\0') {
            browserHtml += " ";
            browserHtml += statusClass;
        }
        browserHtml += "\">" + std::string(status) + "</span></button>"
            "<div class=\"sfx-browser-actions\">"
            "<button class=\"sfx-browser-action\" data-action=\"focus-sfx-rename\" data-arg=\""
            + escapeRml(entry.id) + "\" title=\"Rename\"><span class=\"icon\">&#xeb0a;</span></button>"
            "<button class=\"sfx-browser-action\" data-action=\"duplicate-generated-sfx\" data-arg=\""
            + escapeRml(entry.id) + "\" title=\"Duplicate Sound\"><span class=\"icon\">&#xedef;</span></button>"
            "<button id=\"sfx-delete-" + escapeRml(entry.id)
            + "\" class=\"sfx-browser-action destructive\" data-action=\"remove-generated-sfx\" data-arg=\""
            + escapeRml(entry.id) + "\" title=\"Delete sound, linked audio and WAV\"><span class=\"icon\">&#xeb41;</span></button>"
            "</div></div>";
    }
    if (shown == 0) {
        browserHtml += "<div class=\"sfx-browser-empty\">"
            + std::string(sfxView.browserFilter.empty()
                              ? "No Generated SFX yet."
                              : "No matches.")
            + "</div>";
    }
    browserHtml += "</div>";
    browser->SetInnerRML(browserHtml);

    if (!definition) {
        if (Rml::Element* preview = document_->GetElementById("btn-sfx-preview"))
            preview->SetClass("disabled", true);
        if (Rml::Element* stop = document_->GetElementById("btn-sfx-stop"))
            stop->SetClass("disabled", true);
        if (Rml::Element* generate = document_->GetElementById("btn-sfx-generate")) {
            generate->SetInnerRML("Generate Audio Asset");
            generate->SetClass("disabled", true);
        }
        if (Rml::Element* more = document_->GetElementById("btn-sfx-more"))
            more->SetClass("disabled", true);
        if (Rml::Element* moreMenu = document_->GetElementById("sfx-more-menu"))
            moreMenu->SetClass("hidden", true);
        if (Rml::Element* dialogHost =
                document_->GetElementById("sfx-create-from-current"))
            dialogHost->SetClass("hidden", true);
        body->SetInnerRML(
            "<div class=\"sfx-empty-workspace\">"
            "<span class=\"sfx-empty-title\">No Generated SFX selected</span>"
            "<span class=\"sfx-empty-copy\">Create a sound to start editing its recipe.</span>"
            "<button class=\"panel-btn primary\" data-action=\"toggle-sfx-create-menu\">Create Generated SFX</button>"
            "</div>");
        return;
    }

    if (Rml::Element* preview = document_->GetElementById("btn-sfx-preview"))
        preview->SetClass("disabled", false);
    if (Rml::Element* more = document_->GetElementById("btn-sfx-more"))
        more->SetClass("disabled", false);

    const artcade::sfx::SfxRecipe& recipe = definition->recipe;
    const GeneratedSfxOutputStatus outputStatus =
        generatedSfxOutputStatus(coordinator_.document(), *definition);
    const GeneratedSfxStatusProjection observedStatus =
        generatedSfxEditor_.status(definition->id);
    const bool ready = observedStatus.status == GeneratedSfxObservedStatus::UpToDate;
    const bool generating = observedStatus.status
        == GeneratedSfxObservedStatus::Generating;
    // Generate writes into the project's own folder, so it needs a saved
    // project path -- surfaced here (not just via the Console error the
    // application layer already logs) so the constraint is visible before
    // the click does nothing, not only after. Disabling the button itself
    // (not just describing the constraint in prose next to an active-looking
    // button) is what makes "why didn't that do anything" unreachable.
    const bool projectSaved = generatedSfxEditor_.projectSaved();
    GeneratedSfxGenerationAvailability generationAvailability;
    if (projectSaved)
        generationAvailability =
            generatedSfxEditor_.generationAvailability(definition->id);
    const bool canGenerate = projectSaved && generationAvailability.allowed
        && !generating;
    // First generation creates the stable AudioAssetDef; later clicks regenerate
    // the same WAV in place.
    if (Rml::Element* generateBtn = document_->GetElementById("btn-sfx-generate")) {
        generateBtn->SetInnerRML(generating ? "Generating..."
            : outputStatus == GeneratedSfxOutputStatus::NeedsGeneration
                ? "Generate Audio Asset" : "Regenerate Audio Asset");
        generateBtn->SetClass("disabled", !canGenerate);
    }
    if (Rml::Element* moreMenu = document_->GetElementById("sfx-more-menu"))
        moreMenu->SetClass("hidden", !sfxView.moreMenuOpen);

    if (Rml::Element* dialogHost = document_->GetElementById("sfx-create-from-current")) {
        dialogHost->SetClass("hidden", !sfxView.createFromCurrentOpen);
        if (sfxView.createFromCurrentOpen) {
            GeneratedSfxGenerationAvailability createAvailability;
            if (projectSaved) {
                createAvailability = generatedSfxEditor_.generationAvailability(
                    nextGeneratedSfxId(coordinator_.document()));
            }
            const bool canCreate = projectSaved
                && sfxView.createFromCurrentError.empty()
                && !sfxView.createFromCurrentName.empty()
                && createAvailability.allowed;
            const std::string createError = !sfxView.createFromCurrentError.empty()
                ? sfxView.createFromCurrentError : createAvailability.reason;
            std::string dialogHtml =
                "<div class=\"sfx-modal\">"
                "<span class=\"sfx-modal-title\">Create New Sound</span>"
                "<span class=\"sfx-modal-copy\">Creates an independent sound using the current recipe.<br/>"
                "The original sound will not be changed.</span>"
                "<span class=\"sfx-modal-label\">Name</span>"
                "<input id=\"sfx-create-from-current-name\" class=\"sfx-modal-input\" type=\"text\" "
                "data-action=\"sfx-create-from-current-name\" value=\""
                + escapeRml(sfxView.createFromCurrentName) + "\"/>";
            if (!projectSaved) {
                dialogHtml +=
                    "<span class=\"sfx-modal-error\">Save the project before creating an audio asset</span>"
                    "<div class=\"sfx-modal-actions\">"
                    "<button class=\"panel-btn\" data-action=\"cancel-sfx-create-from-current\">Cancel</button>"
                    "<button class=\"panel-btn\" data-action=\"save-project\">Save Project&#8230;</button>"
                    "<button id=\"btn-sfx-create-from-current-confirm\" class=\"panel-btn primary disabled\">Create and Generate</button>"
                    "</div></div>";
            } else {
                dialogHtml +=
                    "<span class=\"sfx-modal-error\">" + escapeRml(createError) + "</span>"
                    "<div class=\"sfx-modal-actions\">"
                    "<button class=\"panel-btn\" data-action=\"cancel-sfx-create-from-current\">Cancel</button>"
                    "<button class=\"panel-btn primary"
                    + std::string(canCreate ? "" : " disabled")
                    + "\" id=\"btn-sfx-create-from-current-confirm\" data-action=\"confirm-sfx-create-from-current\">Create and Generate</button>"
                    "</div></div>";
            }
            dialogHost->SetInnerRML(dialogHtml);
            if (generatedSfxEditor_.consumeFocusCreateFromCurrentName()) {
                if (Rml::Element* nameInput =
                        document_->GetElementById("sfx-create-from-current-name")) {
                    nameInput->Focus(true);
                }
            }
        } else {
            dialogHost->SetInnerRML("");
        }
    }
    const std::string activePreset = activeGeneratedSfxPresetId(recipe);
    const auto presetButton = [&](std::string_view id, std::string_view label) {
        return "<button class=\"sfx-preset"
             + std::string(activePreset == id ? " active" : "")
             + "\" data-action=\"apply-sfx-preset\" data-arg=\""
             + std::string(id) + "\">" + std::string(label) + "</button>";
    };
    std::string presetMarkup;
    for (const auto& preset : generatedSfxPresetCatalog()) {
        if (!preset.availableForApply) continue;
        presetMarkup += presetButton(preset.id, preset.label);
    }
    const bool justGenerated = ready && sfxView.justGeneratedId == definition->id;
    const std::string outputStatusMarkup =
        !projectSaved ? "\">Save the project before generating an audio asset."
        : !generationAvailability.allowed
            ? " stale\">" + escapeRml(generationAvailability.reason)
        : justGenerated ? " ready\">Audio asset generated"
        : ready || generating
            ? " ready\">" + escapeRml(observedStatus.message)
            : " stale\">" + escapeRml(observedStatus.message);
    // ConsoleMessage carries no asset id or field path (that's the deferred
    // "field-addressable diagnostics" slice), so this can only reflect the
    // console's global counts -- the same numbers status-health already
    // shows -- not "problems belonging to this specific Generated SFX".
    // Reusing status-health/status-dot's own classes (controls.rcss) gets
    // identical dot+color styling here for free.
    std::size_t consoleErrors = 0, consoleWarnings = 0;
    for (const ConsoleMessage& message : coordinator_.consoleLog()) {
        if (message.level == ConsoleMessage::Level::Error) ++consoleErrors;
        else if (message.level == ConsoleMessage::Level::Warning) ++consoleWarnings;
    }
    const bool canDismissGenerationError = observedStatus.status
        == GeneratedSfxObservedStatus::GenerationFailed;
    std::string html = "<div class=\"sfx-summary\"><div class=\"sfx-summary-row\">"
        "<span class=\"sfx-field-label\">Name</span>"
        "<input id=\"sfx-name-input\" class=\"sfx-name\" type=\"text\" data-action=\"commit-sfx-name\" "
        "title=\"Rename this Generated SFX\" value=\""
        + escapeRml(definition->name) + "\"/>"
        "<span class=\"sfx-status" + outputStatusMarkup
        + "</span>"
        + (canDismissGenerationError
            ? "<button class=\"sfx-inline-action\" data-action=\"dismiss-sfx-generation-error\">Dismiss</button>"
            : "")
        + (!projectSaved ? "<button class=\"sfx-inline-action\" data-action=\"save-project\">Save Project&#8230;</button>" : "")
        + "</div>"
        + (!definition->outputPath.empty()
            ? "<div class=\"sfx-output-path\">" + escapeRml(definition->outputPath) + "</div>"
            : "")
        + ((consoleErrors + consoleWarnings) > 0
            ? "<div class=\"sfx-console-issues\"><span class=\"status-health "
              + std::string(consoleErrors != 0 ? "error" : "warning") + "\"><span class=\"status-dot\"></span>"
              + (consoleErrors != 0
                    ? std::to_string(consoleErrors) + (consoleErrors == 1 ? " error" : " errors")
                    : std::to_string(consoleWarnings) + (consoleWarnings == 1 ? " warning" : " warnings"))
              + "</span><button class=\"sfx-inline-action\" data-action=\"open-console-issues\">Show details</button></div>"
            : "")
        + "<div class=\"sfx-presets\">"
          "<span class=\"sfx-field-label\">Apply to current</span>"
        + presetMarkup
        + "<span class=\"sfx-preset-divider\"></span>"
          "<button class=\"sfx-preset sfx-randomize\" data-action=\"randomize-sfx\">Randomize</button>"
        + (activePreset.empty() ? "<span class=\"sfx-custom-badge\">Custom</span>" : "")
        + "</div></div>";

    if (!sfxView.advancedMode) {
        // Simple mode: seven macro sliders standing in for the ~40 raw
        // fields below. The header (always visible, Preview/Stop/Generate/
        // Close) already covers those actions -- Simple mode is short enough
        // that a second copy at the bottom would just be an unexplained
        // duplicate, not a convenience (unlike Logic Board's "+ Add Logic"
        // footer, which duplicates a toolbar button the user may have
        // scrolled well past).
        html += "<div class=\"sfx-simple-content\"><div class=\"sfx-simple\">";
        for (const SfxMacro& macro : kSfxMacros) html += sfxMacroRow(macro, recipe);
        html += "</div>"
                "<button class=\"sfx-mode-toggle-row\" data-action=\"toggle-sfx-mode\">"
                "<span>Advanced settings</span><span class=\"sfx-mode-toggle-chevron\">&#8250;</span>"
                "</button></div>";
        body->SetInnerRML(html);
        if (generatedSfxEditor_.consumeFocusNameField()) {
            if (Rml::Element* nameInput = document_->GetElementById("sfx-name-input"))
                nameInput->Focus();
        }
        return;
    }

    html += "<button class=\"sfx-mode-toggle-row sfx-mode-toggle-back\" data-action=\"toggle-sfx-mode\">"
            "<span class=\"sfx-mode-toggle-chevron\">&#8249;</span><span>Back to Simple</span>"
            "</button><div class=\"sfx-grid\">";

    html += "<div class=\"sfx-section\"><div class=\"sfx-section-title\">Master &amp; Envelope</div>";
    html += sfxInput("Duration (s)", "duration", recipe.durationSeconds);
    html += sfxInput("Master gain", "masterGain", recipe.masterGain);
    html += sfxInput("Attack (s)", "attack", recipe.amplitude.attackSeconds);
    html += sfxInput("Decay (s)", "decay", recipe.amplitude.decaySeconds);
    html += sfxInput("Sustain", "sustain", recipe.amplitude.sustainLevel);
    html += sfxInput("Release (s)", "release", recipe.amplitude.releaseSeconds);
    html += sfxInput("Random seed", "seed", std::to_string(recipe.randomSeed));
    html += "<div class=\"sfx-help\">Recipe schema 1 · generator 2 · mono PCM</div></div>";

    html += "<div class=\"sfx-section\"><div class=\"sfx-section-title\">Primary Voice</div>";
    html += sfxToggle("Enabled", "primary.enabled", recipe.primaryVoice.enabled);
    html += sfxInput("Gain", "primary.gain", recipe.primaryVoice.gain);
    html += sfxInput("Detune", "primary.detune", recipe.primaryVoice.detuneSemitones);
    html += sfxVoiceOscillatorFields(recipe.primaryVoice, "primary");
    html += sfxPitchFields(recipe.primaryVoice.pitch, "primary.pitch");
    html += "</div>";

    // Secondary Voice: one card in the same grid as every other section --
    // collapsed, it's just the header (Enabled toggle visible even then);
    // expanded, it leads with the three fields that matter for "a second
    // layer of the same sound" (Mix/Detune/Copy) before a nested "More
    // settings" reveals the full oscillator/pitch/modulation set, none of it
    // repeating what the compact block already showed. The header used to be
    // emitted as a bare .sfx-grid child with no card wrapper at all, which is
    // why it stretched edge-to-edge with the toggle stranded at the far
    // right -- it was never inside a .sfx-section box to begin with.
    const bool secondaryCollapsed =
        generatedSfxEditor_.sectionCollapsed("secondary-voice");
    html += "<div class=\"sfx-section\">";
    html += sfxSectionHeader("Secondary Voice", "secondary-voice", secondaryCollapsed,
                             "secondary.enabled", recipe.secondaryVoice.enabled);
    if (!secondaryCollapsed) {
        html += sfxSecondaryCompactFields(recipe.secondaryVoice);
        const bool moreCollapsed =
            generatedSfxEditor_.sectionCollapsed("secondary-voice-more");
        html += "<button class=\"sfx-subsection-toggle\" data-action=\"toggle-sfx-section\" "
                "data-arg=\"secondary-voice-more\">"
              + std::string(moreCollapsed ? "More settings &#8250;" : "More settings &#8249;")
              + "</button>";
        if (!moreCollapsed) {
            html += sfxVoiceOscillatorFields(recipe.secondaryVoice, "secondary");
            html += sfxPitchFields(recipe.secondaryVoice.pitch, "secondary.pitch");
        }
    }
    html += "</div>";

    const bool noiseCollapsed =
        generatedSfxEditor_.sectionCollapsed("noise-layer");
    html += "<div class=\"sfx-section\">";
    html += sfxSectionHeader("Noise Layer", "noise-layer", noiseCollapsed,
                             "noise.enabled", recipe.noise.enabled);
    if (!noiseCollapsed) {
        html += sfxInput("Gain", "noise.gain", recipe.noise.gain);
        html += sfxPitchFields(recipe.noise.clock, "noise.pitch");
    }
    html += "</div>";

    html += "<div class=\"sfx-section\"><div class=\"sfx-section-title\">Bit Crusher</div>";
    html += sfxToggle("Enabled", "crusher.enabled", recipe.bitCrusher.enabled);
    html += sfxInput("Quantization bits", "crusher.bits",
                     std::to_string(recipe.bitCrusher.quantizationBits));
    html += sfxInput("Reduction rate", "crusher.rate", recipe.bitCrusher.reductionRateHz);
    html += "</div>";

    html += "<div class=\"sfx-section\"><div class=\"sfx-section-title\">Filter</div>";
    html += sfxInput("Low-pass Hz", "filter.lowPass", recipe.filter.lowPassHz);
    html += sfxToggle("DC blocker", "filter.dcEnabled", recipe.filter.dcBlockEnabled);
    html += sfxInput("DC cutoff Hz", "filter.dcCutoff", recipe.filter.dcBlockCutoffHz);
    html += "</div></div>";
    body->SetInnerRML(html);
    if (generatedSfxEditor_.consumeFocusNameField()) {
        if (Rml::Element* nameInput = document_->GetElementById("sfx-name-input"))
            nameInput->Focus();
    }
}

const char* waveformLabel(artcade::sfx::Waveform value) {
    using artcade::sfx::Waveform;
    switch (value) {
        case Waveform::Square: return "Square";
        case Waveform::Pulse: return "Pulse";
        case Waveform::Triangle: return "Triangle";
        case Waveform::Saw: return "Saw";
    }
    return "Square";
}

const char* qualityLabel(artcade::sfx::OscillatorQuality value) {
    return value == artcade::sfx::OscillatorQuality::Raw ? "Raw" : "Band limited";
}

const char* sweepLabel(artcade::sfx::PitchSweepMode value) {
    return value == artcade::sfx::PitchSweepMode::LinearHz ? "Linear Hz" : "Exponential";
}

std::string sfxInput(const char* label, const char* field, const std::string& value) {
    return std::string("<div class=\"sfx-field\"><span class=\"sfx-field-label\">")
         + label + "</span><input type=\"text\" data-action=\"commit-sfx-field\" data-arg=\""
         + field + "\" value=\"" + escapeRml(value) + "\"/></div>";
}

std::string sfxInput(const char* label, const char* field, float value) {
    return sfxInput(label, field, compactNumber(value));
}

std::string sfxChoice(const char* label, const char* action,
                      const char* field, const char* value) {
    return std::string("<div class=\"sfx-field\"><span class=\"sfx-field-label\">")
         + label + "</span><button class=\"sfx-choice\" data-action=\"" + action
         + "\" data-arg=\"" + field + "\">" + value + "</button></div>";
}

std::string sfxToggle(const char* label, const char* field, bool enabled) {
    return sfxChoice(label, "toggle-sfx-field", field, enabled ? "On" : "Off");
}

std::string sfxPitchFields(const artcade::sfx::PitchParams& pitch,
                           const std::string& prefix) {
    std::string html;
    html += sfxInput("Start frequency", (prefix + ".startHz").c_str(), pitch.startHz);
    html += sfxInput("End frequency", (prefix + ".endHz").c_str(), pitch.endHz);
    html += sfxInput("Sweep curve", (prefix + ".curve").c_str(), pitch.sweepCurve);
    html += sfxChoice("Sweep", "cycle-sfx-field", (prefix + ".sweep").c_str(),
                      sweepLabel(pitch.sweepMode));
    html += sfxInput("Vibrato depth", (prefix + ".vibratoDepth").c_str(),
                     pitch.vibratoDepthSemitones);
    html += sfxInput("Vibrato rate", (prefix + ".vibratoRate").c_str(), pitch.vibratoRateHz);
    html += sfxInput("Arpeggio semitones", (prefix + ".arpSemitones").c_str(),
                     pitch.arpeggioSemitones);
    html += sfxInput("Arpeggio rate", (prefix + ".arpRate").c_str(), pitch.arpeggioRateHz);
    return html;
}

// Waveform, Quality, Duty start/end -- deliberately NOT gain/detune, which
// Secondary Voice's compact block shows as "Mix"/"Detune" instead; Primary
// Voice's Advanced section renders those two inline itself (see caller).
std::string sfxVoiceOscillatorFields(const artcade::sfx::VoiceParams& voice, const char* prefix) {
    std::string html = sfxChoice("Waveform", "cycle-sfx-field",
                      (std::string(prefix) + ".waveform").c_str(), waveformLabel(voice.waveform));
    html += sfxChoice("Quality", "cycle-sfx-field",
                      (std::string(prefix) + ".quality").c_str(), qualityLabel(voice.quality));
    html += sfxInput("Duty start", (std::string(prefix) + ".dutyStart").c_str(), voice.dutyStart);
    html += sfxInput("Duty end", (std::string(prefix) + ".dutyEnd").c_str(), voice.dutyEnd);
    return html;
}

// Secondary Voice's always-visible block once its section is expanded:
// Enabled itself lives in the section header (sfxSectionHeader), so this is
// just the three fields that matter for "a second layer of the same sound".
std::string sfxSecondaryCompactFields(const artcade::sfx::VoiceParams& voice) {
    std::string html = sfxInput("Mix", "secondary.gain", voice.gain);
    html += sfxInput("Detune", "secondary.detune", voice.detuneSemitones);
    html += "<button class=\"panel-btn sfx-copy-primary\" "
            "data-action=\"copy-primary-to-secondary\">Copy Primary Settings</button>";
    return html;
}

std::string sfxSectionHeader(const char* title, const std::string& sectionId, bool collapsed,
                             const char* toggleField, bool toggleOn) {
    std::string html = "<div class=\"sfx-section-head\">"
        "<button class=\"sfx-section-caret\" data-action=\"toggle-sfx-section\" data-arg=\""
        + sectionId + "\">" + (collapsed ? "&#xeb5d;" : "&#xeb5f;") + "</button>"
        "<span class=\"sfx-section-title\">" + title + "</span>";
    if (toggleField) {
        html += "<button class=\"sfx-choice sfx-section-toggle\" data-action=\"toggle-sfx-field\" "
                "data-arg=\"" + std::string(toggleField) + "\">"
              + (toggleOn ? "On" : "Off") + "</button>";
    }
    html += "</div>";
    return html;
}

std::string formatSfxMacroNumber(const SfxMacro& macro, float displayValue) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", macro.decimals, displayValue);
    return std::string(buffer);
}

std::string sfxMacroRow(const SfxMacro& macro, const artcade::sfx::SfxRecipe& recipe) {
    const float sliderValue = sfxMacroValue(recipe, macro.id);
    const float displayValue = sfxMacroDisplayValue(recipe, macro.id);
    return "<div class=\"sfx-macro-row\"><span class=\"sfx-macro-label\">" + std::string(macro.label)
         + "</span><input type=\"range\" class=\"sfx-macro-slider\" min=\"" + compactNumber(macro.sliderMin)
         + "\" max=\"" + compactNumber(macro.sliderMax) + "\" step=\"" + compactNumber(macro.step)
         + "\" value=\"" + compactNumber(sliderValue) + "\" data-action=\"drag-sfx-macro\" data-arg=\""
         + macro.id + "\"/><input type=\"text\" class=\"sfx-macro-input\" id=\"sfx-macro-input-"
         + macro.id + "\" data-action=\"commit-sfx-macro\" data-arg=\"" + macro.id + "\" value=\""
         + formatSfxMacroNumber(macro, displayValue)
         + "\"/><span class=\"sfx-macro-unit\">" + macro.unit + "</span></div>";
}

bool EditorUi::hasOpenContextMenu() const {
    return viewportContextMenuVisible_ || hierarchyContextMenuVisible_
        || assetsContextMenuVisible_ || logicTypeMenuVisible_ || logicMoreMenuVisible_;
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

void EditorUi::toggleLogicMoreMenu() {
    if (!document_) return;
    if (logicMoreMenuVisible_) { hideContextMenus(); return; }
    Rml::Element* trigger = document_->GetElementById("logic-more-trigger");
    Rml::Element* menu = document_->GetElementById("logic-more-menu");
    if (!trigger || !menu) return;
    hideContextMenus();
    const Rml::Vector2f offset = trigger->GetAbsoluteOffset();
    menu->SetProperty("left", std::to_string(static_cast<int>(offset.x)) + "px");
    menu->SetProperty("top",
        std::to_string(static_cast<int>(offset.y + trigger->GetClientHeight())) + "px");
    menu->SetClass("hidden", false);
    trigger->SetClass("open", true);
    logicMoreMenuVisible_ = true;
}

void EditorUi::refreshToolbar() {
    if (!document_) return;
    const bool playing = coordinator_.isPlaying();
    const bool logicWorkspace =
        coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Logic;
    const bool scriptWorkspace =
        coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Script;
    const bool sceneWorkspace =
        coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Scene;
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
    // Collapse/Expand are workspace navigation, not authoring: usable during
    // Play, but meaningless outside the Rules tab (nothing to collapse in
    // Generated Lua). Their disabled state comes from the panel's own
    // collapsedRuleIds_, queried against the board it currently renders.
    const bool logicRulesTab = logicWorkspace && logicView.tab == LogicBoardTab::Rules;
    setEnabled("btn-logic-collapse-all", logicRulesTab && logicBoardEditor_.canCollapseAllRules());
    setEnabled("btn-logic-expand-all", logicRulesTab && logicBoardEditor_.canExpandAllRules());
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
    const ScriptEditorBuffer* scriptBuffer = coordinator_.state().scriptEditor.active();
    // In Script workspace the global toolbar/menu is the visible editor
    // history affordance. A click necessarily blurs the textarea before this
    // handler runs, so route it by workspace; keyboard routing remains strictly
    // focus-based in the listener/application input path.
    const bool scriptUndoRoute = scriptWorkspace && scriptBuffer;
    setEnabledBoth("btn-undo", "menu-undo", scriptUndoRoute
        ? scriptBuffer->canUndo() : (!playing && coordinator_.canUndo()));
    setEnabledBoth("btn-redo", "menu-redo", scriptUndoRoute
        ? scriptBuffer->canRedo() : (!playing && coordinator_.canRedo()));
    setEnabled("btn-script-save", scriptWorkspace && scriptBuffer && scriptBuffer->dirty());
    setEnabled("btn-script-save-all", scriptWorkspace
        && coordinator_.state().scriptEditor.anyDirty());
    // Select/Pan are always present. Tilemap paint tools (Brush/Eraser/…)
    // appear only when the selection supports tilemap editing — same
    // EditorState::activeTool / effectiveTilemapTool(), no second authority.
    {
        const bool toolActionable = !playing && sceneWorkspace
            && coordinator_.document().findScene(coordinator_.state().activeSceneId) != nullptr;
        const EditorTool activeTool = coordinator_.state().activeTool;
        setEnabled("btn-tool-select", toolActionable);
        setEnabled("btn-tool-pan", toolActionable);
        if (Rml::Element* el = document_->GetElementById("btn-tool-select"))
            el->SetClass("active", toolActionable && activeTool == EditorTool::Select);
        if (Rml::Element* el = document_->GetElementById("btn-tool-pan"))
            el->SetClass("active", toolActionable && activeTool == EditorTool::Pan);

        const bool tilemapToolsVisible = toolActionable
            && selectionSupportsTilemapEditing(
                   coordinator_.document(), coordinator_.state(),
                   coordinator_.state().activeSceneId);
        const SceneInstanceDef* paletteInstance = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        const bool tilePaletteAvailable = toolActionable && paletteInstance
            && tilemapHasPaintableTileset(coordinator_.document(), *paletteInstance);
        setEnabled("menu-tile-palette", tilePaletteAvailable);
        if (Rml::Element* el = document_->GetElementById("tilemap-context-tools"))
            el->SetClass("hidden", !tilemapToolsVisible);
        if (Rml::Element* el = document_->GetElementById("tilemap-tool-sep"))
            el->SetClass("hidden", !tilemapToolsVisible);

        const EditorTool paintTool = coordinator_.effectiveTilemapTool();
        const auto setTilemapTool = [&](const char* id, EditorTool tool) {
            setEnabled(id, tilemapToolsVisible);
            if (Rml::Element* el = document_->GetElementById(id))
                el->SetClass("active", tilemapToolsVisible && paintTool == tool);
        };
        setTilemapTool("btn-tool-brush", EditorTool::Brush);
        setTilemapTool("btn-tool-eraser", EditorTool::Eraser);
        setTilemapTool("btn-tool-picker", EditorTool::Picker);
        setTilemapTool("btn-tool-rectangle", EditorTool::Rectangle);
        setTilemapTool("btn-tool-fill", EditorTool::Fill);

        const bool rectangleActive =
            tilemapToolsVisible && paintTool == EditorTool::Rectangle;
        if (Rml::Element* el = document_->GetElementById("tilemap-shape-sep"))
            el->SetClass("hidden", !rectangleActive);
        if (Rml::Element* el = document_->GetElementById("btn-tilemap-rect-solid")) {
            el->SetClass("hidden", !rectangleActive);
            el->SetClass("disabled", !rectangleActive);
            el->SetClass("active", rectangleActive
                && !coordinator_.state().tilemapEditor.rectangleOutlineMode);
        }
        if (Rml::Element* el = document_->GetElementById("btn-tilemap-rect-outline")) {
            el->SetClass("hidden", !rectangleActive);
            el->SetClass("disabled", !rectangleActive);
            el->SetClass("active", rectangleActive
                && coordinator_.state().tilemapEditor.rectangleOutlineMode);
        }

        if (Rml::Element* stampEl = document_->GetElementById("toolbar-tilemap-stamp")) {
            stampEl->SetClass("hidden", !tilemapToolsVisible);
            if (!tilemapToolsVisible) {
                stampEl->SetInnerRML("");
            } else {
                const SceneInstanceDef* stampInst =
                    coordinator_.document().findInstanceInScene(
                        coordinator_.state().activeSceneId,
                        coordinator_.selection().primaryEntity);
                const std::optional<TilemapTileStamp>& stamp =
                    coordinator_.state().tilemapEditor.stamp;
                if (!stampInst || !stampInst->tilemap.has_value()
                    || !stamp
                    || stamp->sourceTilesetAssetId != stampInst->tilemap->tilesetAssetId) {
                    stampEl->SetInnerRML("Stamp: <span class=\"value\">none</span>");
                } else if (stamp->width == 1 && stamp->height == 1) {
                    stampEl->SetInnerRML("Stamp: <span class=\"value\">1&#215;1</span>");
                } else {
                    stampEl->SetInnerRML(
                        "Stamp: <span class=\"value\">" + std::to_string(stamp->width)
                        + "&#215;" + std::to_string(stamp->height) + "</span>");
                }
            }
        }
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
    const bool gridActionable = !playing && sceneWorkspace && hasScene;
    setEnabledBoth("btn-fit-view",     "menu-fit-view",     gridActionable);
    setEnabledBoth("btn-grid-visible", "menu-grid-visible", gridActionable);
    // Zoom, unlike Grid/Snap/Fit, tracks the PlaySession's scene while
    // playing (see the zoom-in/out and reset-zoom handlers) — Play always has
    // a real scene, so it stays available then. Only truly nothing-to-zoom
    // (no scene, not playing) disables it.
    const bool canZoom = sceneWorkspace && (playing || hasScene);
    setEnabledBoth("btn-zoom-in",  "menu-zoom-in",   canZoom);
    setEnabledBoth("btn-zoom-out", "menu-zoom-out",  canZoom);
    setEnabledBoth("toolbar-zoom", "menu-reset-zoom", canZoom);

    // Central Scene View empty state: shown only when no scene exists to edit.
    if (Rml::Element* empty = document_->GetElementById("viewport-empty")) {
        empty->SetClass("hidden", hasScene || playing || !sceneWorkspace);
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
        coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Logic;
    const bool scriptWorkspace =
        coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Script;

    std::string contextText = playing ? "Play" : "Edit";
    if (logicWorkspace) {
        contextText += "  |  Logic";
        if (coordinator_.state().logicBoardEditor.objectTypeId)
            contextText += "  |  " + *coordinator_.state().logicBoardEditor.objectTypeId;
        if (playing) contextText += " (read-only)";
    } else if (scriptWorkspace) {
        contextText += "  |  Script";
        if (const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active()) {
            if (const ScriptAssetDef* asset = coordinator_.document().findScriptAsset(
                    buffer->scriptAssetId)) {
                contextText += "  |  " + (asset->name.empty() ? asset->assetId : asset->name);
            }
        }
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
    const bool dirty = coordinator_.document().isDirty()
        || coordinator_.state().scriptEditor.anyDirty();
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

bool EditorUi::hasLogicKeyCapture() const {
    return logicBoardEditor_.hasKeyCapture();
}

bool EditorUi::captureLogicKey(LogicKey key) {
    return logicBoardEditor_.captureKey(key);
}

bool EditorUi::cancelLogicKeyCapture() {
    return logicBoardEditor_.cancelKeyCapture();
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

void EditorUi::syncGeneratedSfxPreviewPlaying(bool playing) {
    if (!document_ || !generatedSfxEditor_.viewModel().selectedId) return;
    if (Rml::Element* stop = document_->GetElementById("btn-sfx-stop"))
        stop->SetClass("disabled", !playing);
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
    // Same staleness guard for the Logic Board's "..." menu.
    if (logicMoreMenuVisible_ && action != "toggle-logic-more-menu") {
        hideContextMenus();
    }

    // Inspector Add Component menu: toggle it open/closed, and close it whenever a
    // component is actually added (the add invalidates the Inspector, which then
    // re-renders without the menu). The coordinator still guards the commands.
    if (action == "toggle-add-component") {
        if (!coordinator_.isPlaying()) inspector_.toggleAddMenu(document_, coordinator_);
        return;
    }
    if (action == "add-sprite-renderer" || action == "add-sprite-animator"
        || action == "add-box-collider"
        || action == "add-linear-mover" || action == "add-top-down"
        || action == "add-platformer" || action == "add-auto-destroy"
        || action == "add-camera-target"
        || action == "add-tilemap-component") {
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
        || action == "set-sprite-animation" || action == "set-tilemap-tileset"
        || action == "set-animator-default-clip" || action == "attach-script") {
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
    if (action == "toggle-logic-more-menu") {
        if (coordinator_.isPlaying()) return;
        toggleLogicMoreMenu();
        return;
    }
    if (action == "toggle-inspector-section") {
        inspector_.toggleSection(document_, coordinator_, arg);
        return;
    }
    if (action == "toggle-logic-rule-collapsed") {
        logicBoardEditor_.toggleRuleCollapsed(arg);
        // Collapse All/Expand All now live in the static toolbar, so their
        // disabled state isn't refreshed by the panel's own re-render — sync
        // it explicitly, or a single collapse/expand leaves them stale.
        refreshToolbar();
        return;
    }
    if (action == "collapse-all-logic-rules") {
        logicBoardEditor_.collapseAllRules();
        refreshToolbar();
        return;
    }
    if (action == "expand-all-logic-rules") {
        logicBoardEditor_.expandAllRules();
        refreshToolbar();
        return;
    }
    if (action == "select-logic-object-type") {
        hideContextMenus();   // then fall through to execute the pick
    } else if (action == "set-logic-key") {
        logicBoardEditor_.closeDropdown();   // then fall through to execute the pick
    }

    if (handleProjectFileAction(action, arg, value)) return;
    if (handleConsoleAction(action, arg, value)) return;
    if (handleGeneratedSfxAction(action, arg, value)) return;
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
    if (action == "deselect-entity") {
        // Inspector breadcrumb: the only deliberate way back to the Scene
        // Inspector (Escape no longer deselects - see routeGlobalEscape).
        coordinator_.apply(SelectEntityIntent{INVALID_ENTITY});
    } else if (action == "add-sprite-renderer") {
        addSpriteRenderer(coordinator_);
    } else if (action == "add-sprite-animator") {
        addSpriteAnimator(coordinator_);
    } else if (action == "add-camera-target") {
        addCameraTarget(coordinator_);
    } else if (action == "remove-camera-target") {
        removeCameraTarget(coordinator_);
    } else if (action == "remove-sprite-renderer") {
        removeSpriteRenderer(coordinator_);
    } else if (action == "toggle-instance-visible") {
        const SceneId& sceneId = coordinator_.state().activeSceneId;
        const SceneInstanceDef* inst =
            coordinator_.document().findInstanceInScene(sceneId, selected);
        if (inst) {
            coordinator_.execute(
                SetInstanceVisibleCommand{sceneId, selected, !inst->visible});
        }
    } else if (action == "toggle-sprite-visible") {
        const SpriteRenderView resolved = resolveSpriteRenderer(
            coordinator_.document(), coordinator_.state().activeSceneId,
            coordinator_.selection().primaryEntity);
        if (resolved.present) setSpriteRendererVisible(coordinator_, !resolved.visible);
    } else if (action == "set-sprite-asset") {
        setSpriteRendererAsset(coordinator_, arg);   // arg = assetId ("" clears)
    } else if (action == "set-sprite-animation") {
        // Assign-only: opening the editor for review already has its own
        // dedicated action (open-sprite-animation, the Edit button), so this
        // no longer pops the editor open as a side effect of picking a clip.
        if (!coordinator_.isPlaying()) setSpriteRendererAnimation(coordinator_, arg);
    } else if (action == "set-sprite-default-clip") {
        setSpriteDefaultClip(coordinator_, arg);
    } else if (action == "commit-sprite-speed") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed) coordinator_.logError("Sprite speed is not a number");
        else setSpritePlaybackSpeed(coordinator_, *parsed);
    } else if (action == "toggle-sprite-autoplay") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (type && type->spritePresentation) {
            if (const auto* animation = std::get_if<SpritePresentationAnimation>(
                    &type->spritePresentation->source)) {
                setSpriteAutoPlay(coordinator_, !animation->autoPlay);
            }
        }
    } else if (action == "set-animator-default-clip") {
        const SceneId& sceneId = coordinator_.state().activeSceneId;
        const SceneInstanceDef* inst =
            coordinator_.document().findInstanceInScene(sceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (!inst || !type || !type->spriteAnimator) {
            coordinator_.logError("Object Type has no SpriteAnimator");
        } else {
            const ResolvedSpriteAnimator resolved = resolveSpriteAnimator(*type, *inst);
            if (resolved.origin == ComponentOrigin::InstanceOverride
                && inst->spriteAnimatorOverride) {
                SpriteAnimatorOverride delta = *inst->spriteAnimatorOverride;
                delta.capabilityEnabled.reset();
                if ((!delta.animationAssetId
                     || *delta.animationAssetId == type->spriteAnimator->animationAssetId)
                    && arg == type->spriteAnimator->defaultClipId) {
                    delta.defaultClipId.reset();
                } else {
                    delta.defaultClipId = arg;
                }
                if (!delta.defaultClipId && !delta.autoPlay && !delta.playbackSpeed
                    && !delta.animationAssetId) {
                    coordinator_.execute(
                        ClearInstanceAnimatorOverrideCommand{sceneId, selected});
                } else {
                    coordinator_.execute(SetInstanceAnimatorOverrideCommand{
                        sceneId, selected, std::move(delta)});
                }
            } else {
                coordinator_.execute(
                    SetObjectTypeInitialClipCommand{inst->objectTypeId, arg});
            }
        }
    } else if (action == "commit-animator-speed-ot") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Animator speed is not a number");
        } else if (selected == INVALID_ENTITY) {
            coordinator_.logError("No selected instance");
        } else {
            const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
                coordinator_.state().activeSceneId, selected);
            if (!inst) {
                coordinator_.logError("No selected instance");
            } else {
                coordinator_.execute(SetObjectTypePlaybackSpeedCommand{
                    inst->objectTypeId, *parsed});
            }
        }
    } else if (action == "toggle-animator-autoplay-ot") {
        const SceneId& sceneId = coordinator_.state().activeSceneId;
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            sceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (inst && type && type->spriteAnimator) {
            coordinator_.execute(SetObjectTypeAutoPlayCommand{
                inst->objectTypeId, !type->spriteAnimator->autoPlay});
        }
    } else if (action == "override-animator-instance") {
        const SceneId& sceneId = coordinator_.state().activeSceneId;
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            sceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (inst && type && type->spriteAnimator) {
            SpriteAnimatorOverride delta = inst->spriteAnimatorOverride.value_or(
                SpriteAnimatorOverride{});
            // Seed explicit deltas from current OT values so provenance flips
            // without changing the resolved numbers.
            delta.playbackSpeed = type->spriteAnimator->playbackSpeed;
            delta.autoPlay = type->spriteAnimator->autoPlay;
            coordinator_.execute(SetInstanceAnimatorOverrideCommand{
                sceneId, selected, std::move(delta)});
        }
    } else if (action == "commit-animator-speed") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Animator speed is not a number");
        } else if (selected == INVALID_ENTITY) {
            coordinator_.logError("No selected instance");
        } else {
            const SceneId& sceneId = coordinator_.state().activeSceneId;
            const SceneInstanceDef* inst =
                coordinator_.document().findInstanceInScene(sceneId, selected);
            const EntityDef* type = inst
                ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
            if (!inst || !type || !type->spriteAnimator) {
                coordinator_.logError("Object Type has no SpriteAnimator");
            } else if (!inst->spriteAnimatorOverride) {
                coordinator_.logError("Override for this instance first");
            } else {
                SpriteAnimatorOverride delta = *inst->spriteAnimatorOverride;
                delta.capabilityEnabled.reset();
                if (*parsed == type->spriteAnimator->playbackSpeed) delta.playbackSpeed.reset();
                else delta.playbackSpeed = *parsed;
                if (!delta.defaultClipId && !delta.autoPlay && !delta.playbackSpeed
                    && !delta.animationAssetId) {
                    coordinator_.execute(ClearInstanceAnimatorOverrideCommand{sceneId, selected});
                } else {
                    coordinator_.execute(SetInstanceAnimatorOverrideCommand{
                        sceneId, selected, std::move(delta)});
                }
            }
        }
    } else if (action == "toggle-animator-autoplay") {
        const SceneId& sceneId = coordinator_.state().activeSceneId;
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            sceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (inst && type && type->spriteAnimator && inst->spriteAnimatorOverride) {
            const ResolvedSpritePresentation resolved =
                resolveSpritePresentation(*type, *inst);
            if (resolved.animator) {
                const bool next = !resolved.animator->autoPlay;
                SpriteAnimatorOverride delta = *inst->spriteAnimatorOverride;
                delta.capabilityEnabled.reset();
                if (next == type->spriteAnimator->autoPlay) delta.autoPlay.reset();
                else delta.autoPlay = next;
                if (!delta.defaultClipId && !delta.autoPlay && !delta.playbackSpeed
                    && !delta.animationAssetId) {
                    coordinator_.execute(ClearInstanceAnimatorOverrideCommand{sceneId, selected});
                } else {
                    coordinator_.execute(SetInstanceAnimatorOverrideCommand{
                        sceneId, selected, std::move(delta)});
                }
            }
        }
    } else if (action == "reset-sprite-override") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (type && type->spritePresentation) {
            coordinator_.execute(SetInstanceSpritePresentationOverrideCommand{
                coordinator_.state().activeSceneId, selected, std::nullopt});
        } else {
            coordinator_.execute(ClearInstanceSpriteOverrideCommand{
                coordinator_.state().activeSceneId, selected});
        }
    } else if (action == "reset-animator-override") {
        coordinator_.execute(ClearInstanceAnimatorOverrideCommand{
            coordinator_.state().activeSceneId, selected});
    } else if (action == "remove-sprite-animator-type") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        if (inst) {
            coordinator_.execute(
                RemoveSpriteAnimatorFromObjectTypeCommand{inst->objectTypeId});
        }
    } else if (action == "attach-script") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (!inst || !type) {
            coordinator_.logError("No selected Object Type");
        } else {
            const ScriptComponent current = type->scripts.value_or(ScriptComponent{});
            coordinator_.execute(AddScriptAttachmentCommand{
                inst->objectTypeId,
                ScriptAttachmentDef{nextScriptAttachmentId(current), arg, true},
                current.attachments.size()});
        }
    } else if (action == "remove-script-attachment"
               || action == "toggle-script-attachment"
               || action == "move-script-attachment-up"
               || action == "move-script-attachment-down") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (!inst || !type || !type->scripts) {
            coordinator_.logError("Object Type has no Script attachments");
        } else if (action == "remove-script-attachment") {
            coordinator_.execute(
                RemoveScriptAttachmentCommand{inst->objectTypeId, arg});
        } else {
            const auto found = std::find_if(
                type->scripts->attachments.begin(), type->scripts->attachments.end(),
                [&](const ScriptAttachmentDef& attachment) { return attachment.id == arg; });
            if (found == type->scripts->attachments.end()) {
                coordinator_.logError("Unknown Script attachment");
            } else if (action == "toggle-script-attachment") {
                coordinator_.execute(SetScriptAttachmentEnabledCommand{
                    inst->objectTypeId, arg, !found->enabled});
            } else {
                const std::size_t index = static_cast<std::size_t>(
                    found - type->scripts->attachments.begin());
                if (action == "move-script-attachment-up" && index > 0) {
                    coordinator_.execute(MoveScriptAttachmentCommand{
                        inst->objectTypeId, arg, index - 1});
                } else if (action == "move-script-attachment-down"
                           && index + 1 < type->scripts->attachments.size()) {
                    coordinator_.execute(MoveScriptAttachmentCommand{
                        inst->objectTypeId, arg, index + 1});
                }
            }
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
    } else if (action == "add-auto-destroy") {
        addAutoDestroy(coordinator_);
    } else if (action == "remove-auto-destroy") {
        removeAutoDestroy(coordinator_);
    } else if (action == "commit-auto-destroy-lifespan") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Auto Destroy lifetime is not a number");
        } else {
            setAutoDestroyLifespan(coordinator_, *parsed);
        }
    } else if (action == "add-top-down") {
        addTopDownController(coordinator_);
    } else if (action == "remove-top-down") {
        removeTopDownController(coordinator_);
    } else if (action == "commit-topdown-speed"
               || action == "commit-topdown-acceleration"
               || action == "commit-topdown-friction") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Top Down Controller value is not a number");
        } else if (action == "commit-topdown-speed") {
            setTopDownControllerSpeed(coordinator_, *parsed);
        } else if (action == "commit-topdown-acceleration") {
            setTopDownControllerAcceleration(coordinator_, *parsed);
        } else {
            setTopDownControllerFriction(coordinator_, *parsed);
        }
    } else if (action == "toggle-topdown-four-directions") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, selected);
        const EntityDef* type = inst
            ? coordinator_.document().findObjectType(inst->objectTypeId) : nullptr;
        if (type && type->topDownController) {
            setTopDownControllerFourDirections(
                coordinator_, !type->topDownController->fourDirections);
        }
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
    } else if (action == "commit-platformer-coyote") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Platformer coyote time is not a number");
        else setPlatformerCoyoteTime(coordinator_, *parsed);
    } else if (action == "commit-platformer-jump-buffer") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Platformer jump buffer is not a number");
        else setPlatformerJumpBuffer(coordinator_, *parsed);
    } else if (action == "commit-platformer-climb") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) coordinator_.logError("Platformer climb speed is not a number");
        else setPlatformerClimbSpeed(coordinator_, *parsed);
    } else if (action == "commit-transform-position-x") {
        commitInspectorTransformField(coordinator_, selected,
                                      InspectorTransformField::PositionX, value);
    } else if (action == "commit-transform-position-y") {
        commitInspectorTransformField(coordinator_, selected,
                                      InspectorTransformField::PositionY, value);
    } else if (action == "commit-transform-rotation") {
        commitInspectorTransformField(coordinator_, selected,
                                      InspectorTransformField::RotationDegrees, value);
    } else if (action == "commit-transform-scale-x") {
        commitInspectorTransformField(coordinator_, selected,
                                      InspectorTransformField::ScaleX, value);
    } else if (action == "commit-transform-scale-y") {
        commitInspectorTransformField(coordinator_, selected,
                                      InspectorTransformField::ScaleY, value);
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
    } else if (action == "commit-scene-background-r"
               || action == "commit-scene-background-g"
               || action == "commit-scene-background-b"
               || action == "commit-scene-background-a") {
        const SceneDef* scene =
            coordinator_.document().findScene(coordinator_.state().activeSceneId);
        const std::optional<float> parsed = parseNumberField(value);
        if (!scene) {
            coordinator_.logError("No selected scene");
        } else if (!parsed.has_value()) {
            coordinator_.logError("Scene background component is not a number");
        } else {
            Vec4 color = scene->backgroundColor;
            if (action == "commit-scene-background-r") color.r = *parsed;
            else if (action == "commit-scene-background-g") color.g = *parsed;
            else if (action == "commit-scene-background-b") color.b = *parsed;
            else color.a = *parsed;
            coordinator_.execute(
                SetSceneBackgroundCommand{coordinator_.state().activeSceneId, color});
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
    } else if (action == "commit-camera-target-offset-x"
               || action == "commit-camera-target-offset-y"
               || action == "commit-camera-target-follow-speed") {
        const SceneInstanceDef* inst = selected != INVALID_ENTITY
            ? coordinator_.document().findInstanceInScene(coordinator_.state().activeSceneId, selected)
            : nullptr;
        const std::optional<float> parsed = parseNumberField(value);
        if (!inst || !inst->cameraTarget) {
            coordinator_.logError("No selected Camera Target component");
        } else if (!parsed.has_value()) {
            coordinator_.logError("Camera Target value is not a number");
        } else if (action == "commit-camera-target-follow-speed") {
            setCameraTargetFollowSpeed(coordinator_, *parsed);
        } else {
            Vec2 offset{inst->cameraTarget->offsetX, inst->cameraTarget->offsetY};
            if (action == "commit-camera-target-offset-x") offset.x = *parsed;
            else offset.y = *parsed;
            setCameraTargetOffset(coordinator_, offset);
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
    } else if (action == "open-tilemap-tileset-editor") {
        // Empty-state button under the Tilemap section (the palette sheet's
        // own double-click is routed app-side, tile_palette_input.cpp): no
        // data-arg, so this resolves the tileset from the selected instance's
        // own TilemapComponent.
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
        if (coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Script
            && coordinator_.state().scriptEditor.active()) scriptEditor_.undo();
        else coordinator_.undo();
    } else if (action == "redo") {
        if (coordinator_.state().centerWorkspaceMode == CenterWorkspaceMode::Script
            && coordinator_.state().scriptEditor.active()) scriptEditor_.redo();
        else coordinator_.redo();
    } else if (action == "open-script-workspace") {
        coordinator_.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Script});
    } else if (action == "activate-script") {
        coordinator_.apply(ActivateScriptBufferIntent{arg});
    } else if (action == "save-script") {
        if (const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active()) {
            if (saveScriptRequest_) saveScriptRequest_(buffer->scriptAssetId);
        }
    } else if (action == "save-all-scripts") {
        if (saveAllScriptsRequest_) saveAllScriptsRequest_();
    } else if (action == "close-script") {
        if (closeScriptRequest_) closeScriptRequest_(arg);
    } else if (action == "find-script" || action == "commit-script-search") {
        scriptEditor_.findNext(value);
    } else if (action == "goto-script-line" || action == "commit-script-line") {
        scriptEditor_.goToLine(arg.empty() ? value : arg);
    } else if (action == "select-script-api") {
        scriptEditor_.selectApiEntry(arg);
    } else if (action == "insert-script-api") {
        scriptEditor_.insertApiSnippet(arg);
    } else if (action == "accept-script-completion") {
        char* end = nullptr;
        const unsigned long index = std::strtoul(arg.c_str(), &end, 10);
        if (end && *end == '\0')
            scriptEditor_.acceptCompletionAt(static_cast<std::size_t>(index));
    } else if (action == "validate-script") {
        scriptEditor_.validateActive();
    } else if (action == "restart-script-play") {
        if (restartScriptsRequest_) restartScriptsRequest_();
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
        if (playProjectRequest_) playProjectRequest_();
    } else if (action == "play-current-scene") {
        if (playCurrentSceneRequest_) playCurrentSceneRequest_();
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
        const bool logicWorkspace = coordinator_.state().centerWorkspaceMode
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
    } else if (action == "add-tilemap-entity") {
        hideContextMenus();
        addTilemapEntity(coordinator_);
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
    } else if (action == "open-script") {
        if (openScriptRequest_) openScriptRequest_(arg);
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
    } else if (action == "import-script") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before importing scripts");
            return true;
        }
        if (importAssetRequest_) importAssetRequest_(AssetKind::Script);
    } else if (action == "create-script") {
        if (coordinator_.isPlaying()) {
            coordinator_.logWarning("Stop Play before creating scripts");
            return true;
        }
        if (createScriptRequest_) createScriptRequest_();
    } else if (action == "remove-image-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveImageAssetCommand{arg});
    } else if (action == "remove-audio-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveAudioAssetCommand{arg});
    } else if (action == "remove-font-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveFontAssetCommand{arg});
    } else if (action == "remove-script-asset") {
        if (!arg.empty() && removeScriptRequest_) removeScriptRequest_(arg);
    } else {
        return false;
    }
    return true;
}

void EditorUi::handleScriptTextChanged(const std::string& value) {
    scriptEditor_.textChanged(value);
}

void EditorUi::handleScriptCursorChanged() {
    scriptEditor_.cursorChanged();
}

void EditorUi::setScriptEditorFocused(bool focused) {
    scriptEditor_.setFocused(focused);
    refreshToolbar();
}

void EditorUi::handleScriptEditorShortcut(int key, bool control, bool shift, bool alt) {
    if (key == Rml::Input::KI_ESCAPE) {
        scriptEditor_.hideCompletions();
        return;
    }
    if (key == Rml::Input::KI_TAB) {
        if (shift) scriptEditor_.outdent();
        else scriptEditor_.insertSpacesForTab();
    } else if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER) {
        scriptEditor_.autoIndentNewline();
    } else if (control && key == Rml::Input::KI_SPACE) {
        scriptEditor_.showCompletions();
    } else if (control && key == Rml::Input::KI_OEM_2) { // Ctrl+/
        scriptEditor_.toggleComment();
    } else if (control && (key == Rml::Input::KI_M)) {
        scriptEditor_.jumpToMatchingBracket();
    } else if (control && shift && key == Rml::Input::KI_L) {
        scriptEditor_.duplicateLines();
    } else if (alt && key == Rml::Input::KI_UP) {
        scriptEditor_.moveLines(-1);
    } else if (alt && key == Rml::Input::KI_DOWN) {
        scriptEditor_.moveLines(1);
    } else if (control && key == Rml::Input::KI_S) {
        if (const ScriptEditorBuffer* buffer = coordinator_.state().scriptEditor.active()) {
            if (saveScriptRequest_) saveScriptRequest_(buffer->scriptAssetId);
        }
    } else if (control && key == Rml::Input::KI_F) {
        if (document_) {
            if (Rml::Element* search = document_->GetElementById("script-toolbar-search"))
                search->Focus(true);
        }
    } else if (control && (key == Rml::Input::KI_Y || (shift && key == Rml::Input::KI_Z))) {
        scriptEditor_.redo();
    } else if (control && key == Rml::Input::KI_Z) {
        scriptEditor_.undo();
    }
}

bool EditorUi::handleGeneratedSfxAction(const std::string& action,
                                        const std::string& arg,
                                        const std::string& value) {
    std::string dispatchedValue = value;
    const auto* descriptor = findGeneratedSfxEditorAction(action);
    if (!descriptor) return false;
    if (descriptor->action == GeneratedSfxEditorAction::ConfirmCreateFromCurrent
        && dispatchedValue.empty() && document_) {
        if (Rml::Element* nameInput =
                document_->GetElementById("sfx-create-from-current-name")) {
            if (auto* control =
                    rmlui_dynamic_cast<Rml::ElementFormControl*>(nameInput)) {
                dispatchedValue = control->GetValue();
            }
        }
    }
    const GeneratedSfxEditorUpdate update = generatedSfxEditor_.dispatch(
        descriptor->action, arg, dispatchedValue);
    if (update.deferRefresh) deferGeneratedSfxRefresh();
    else if (update.refresh) refreshGeneratedSfxEditor();
    return update.handled;
}

bool EditorUi::handleConsoleAction(const std::string& action, const std::string& arg,
                                   const std::string& value) {
    if (action == "select-console") {
        const std::size_t index = static_cast<std::size_t>(
            std::strtoul(arg.c_str(), nullptr, 10));
        console_.select(index, document_, coordinator_);
        const ConsoleMessage* message = coordinator_.consoleMessage(index);
        if (message && message->scriptSource && openScriptRequest_) {
            const ConsoleMessage::ScriptSource source = *message->scriptSource;
            openScriptRequest_(source.scriptAssetId);
            scriptEditor_.refresh();
            if (source.line > 0)
                scriptEditor_.goToLocation(source.line, std::max(1, source.column));
        }
    } else if (action == "copy-console") {
        copySelectedConsoleMessage();
    } else if (action == "clear-console") {
        coordinator_.clearConsole();
    } else if (action == "toggle-console") {
        coordinator_.apply(ToggleConsoleIntent{});
    } else if (action == "toggle-tile-palette-dock") {
        coordinator_.apply(ToggleTilePaletteDockIntent{});
    } else if (action == "show-tile-palette-dock") {
        coordinator_.apply(SetTilePaletteDockVisibleIntent{true});
    } else if (action == "tile-palette-fit-content"
               || action == "tile-palette-fit-selection"
               || action == "tile-palette-fit-sheet"
               || action == "tile-palette-zoom-1"
               || action == "tile-palette-zoom-2"
               || action == "tile-palette-zoom-3"
               || action == "tile-palette-zoom-4"
               || action == "tile-palette-toggle-grid") {
        if (coordinator_.isPlaying()) return true;
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (!inst || !tilemapHasPaintableTileset(coordinator_.document(), *inst)) return true;
        const AssetId tilesetId = inst->tilemap->tilesetAssetId;
        if (action == "tile-palette-fit-content") {
            if (!coordinator_.uiState().tilePaletteDockVisible) {
                coordinator_.apply(SetTilePaletteDockVisibleIntent{true});
            }
            coordinator_.apply(RequestTilePaletteFitIntent{tilesetId, TilePaletteFitKind::Content});
        } else if (action == "tile-palette-fit-selection") {
            if (!coordinator_.uiState().tilePaletteDockVisible) {
                coordinator_.apply(SetTilePaletteDockVisibleIntent{true});
            }
            coordinator_.apply(RequestTilePaletteFitIntent{tilesetId, TilePaletteFitKind::Selection});
        } else if (action == "tile-palette-fit-sheet") {
            if (!coordinator_.uiState().tilePaletteDockVisible) {
                coordinator_.apply(SetTilePaletteDockVisibleIntent{true});
            }
            coordinator_.apply(RequestTilePaletteFitIntent{tilesetId, TilePaletteFitKind::Sheet});
        } else if (action == "tile-palette-toggle-grid") {
            const auto it = coordinator_.state().tilemapEditor.paletteViews.find(tilesetId);
            const bool visible = it == coordinator_.state().tilemapEditor.paletteViews.end()
                || it->second.gridVisible;
            coordinator_.apply(SetTilePaletteGridVisibleIntent{tilesetId, !visible});
        } else {
            int step = 2;
            if (action == "tile-palette-zoom-1") step = 1;
            else if (action == "tile-palette-zoom-2") step = 2;
            else if (action == "tile-palette-zoom-3") step = 3;
            else step = 4;
            const float scaleAfter = tilePaletteScaleForStep(step);
            const auto viewIt = coordinator_.state().tilemapEditor.paletteViews.find(tilesetId);
            const TilePaletteViewState before = viewIt != coordinator_.state().tilemapEditor.paletteViews.end()
                ? viewIt->second : TilePaletteViewState{};
            const ViewportRect hole = elementContentRectFromDocument(document_, "tile-palette");
            if (hole.valid() && before.textureScale > 0.f
                && before.textureScale != scaleAfter) {
                // Remap only — leave clamp to the next input frame, which has
                // the real texture size via tilePaletteProjectionForHole.
                const Vec2 scroll = remapTilePaletteScrollForZoom(
                    before.textureScale, scaleAfter, before.scrollOffset,
                    static_cast<float>(hole.width) * 0.5f,
                    static_cast<float>(hole.height) * 0.5f);
                coordinator_.apply(SetTilePaletteViewIntent{tilesetId, scaleAfter, scroll});
            } else {
                coordinator_.apply(SetTilePaletteZoomIntent{tilesetId, scaleAfter});
            }
        }
    } else if (action == "open-console-issues") {
        // #status-health's own click: force the console open (never close it
        // if it's already open -- unlike the generic toggle-console the View
        // menu still uses) and narrow the filter to whichever severity the
        // button itself is currently displaying, so "N warnings" leads
        // straight to those N warnings instead of an unfiltered log.
        std::size_t errors = 0, warnings = 0;
        for (const ConsoleMessage& message : coordinator_.consoleLog()) {
            if (message.level == ConsoleMessage::Level::Error) ++errors;
            else if (message.level == ConsoleMessage::Level::Warning) ++warnings;
        }
        if (!coordinator_.uiState().consoleVisible) coordinator_.apply(ToggleConsoleIntent{});
        if (errors != 0) {
            coordinator_.apply(SetConsoleShowErrorIntent{true});
            coordinator_.apply(SetConsoleShowWarningIntent{false});
            coordinator_.apply(SetConsoleShowInfoIntent{false});
        } else if (warnings != 0) {
            coordinator_.apply(SetConsoleShowWarningIntent{true});
            coordinator_.apply(SetConsoleShowErrorIntent{false});
            coordinator_.apply(SetConsoleShowInfoIntent{false});
        }
        // Ready (no errors/warnings): just open, leave existing filters alone.
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
    } else if (action == "resize-tile-palette") {
        // Splitter sits above the dock; drag up → taller dock (distance from
        // mouse to the bottom of #center-content), matching console resize.
        float contentBottom = static_cast<float>(dims.y);
        if (Rml::Element* content = document_->GetElementById("center-content")) {
            const Rml::Vector2f off = content->GetAbsoluteOffset(Rml::BoxArea::Border);
            contentBottom = off.y + content->GetBox().GetSize(Rml::BoxArea::Border).y;
        }
        coordinator_.apply(ResizePanelIntent{ResizePanelIntent::Panel::TilePaletteDock,
                                             contentBottom - mouseY});
        if (Rml::Element* el = document_->GetElementById("tile-palette-dock"))
            el->SetProperty("height", px(coordinator_.uiState().tilePaletteDockHeight));
    }
}

void EditorUi::beginSfxMacroDrag(const std::string& macroId) {
    generatedSfxEditor_.beginMacroDrag(macroId);
}

void EditorUi::handleSfxMacroChange(const std::string& macroId, float value) {
    const GeneratedSfxMacroChange change =
        generatedSfxEditor_.changeMacro(macroId, value);
    if (!change.handled) return;
    if (change.committed) {
        refreshGeneratedSfxEditor();
        return;
    }
    if (!document_) return;
    if (Rml::Element* input =
            document_->GetElementById("sfx-macro-input-" + macroId)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            if (const SfxMacro* macro = findSfxMacro(macroId))
                control->SetValue(formatSfxMacroNumber(*macro, change.displayValue));
        }
    }
}

void EditorUi::commitSfxMacroDrag() {
    if (generatedSfxEditor_.commitMacroDrag()) refreshGeneratedSfxEditor();
}

} // namespace ArtCade::EditorNative
