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
#include "editor-native/commands/project_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"

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
#include <utility>
#include <vector>

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

void restoreFormValue(Rml::Element* element) {
    if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element))
        control->SetValue(attribute(element, "value"));
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

const SpriteAnimationClipDef* selectedAnimationClip(const SpriteAnimationAssetDef& asset,
                                                    const SpriteAnimationEditorState& state) {
    if (!state.selectedClipId) return nullptr;
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == *state.selectedClipId) return &clip;
    }
    return nullptr;
}

bool frameMatchesEditorGrid(const SpriteAnimationFrameDef& frame,
                            const SpriteAnimationEditorState& state) {
    if (frame.width != state.sliceFrameWidth || frame.height != state.sliceFrameHeight) {
        return false;
    }
    if (frame.x < state.sliceMargin || frame.y < state.sliceMargin) return false;
    const int stepX = state.sliceFrameWidth + state.sliceSpacing;
    const int stepY = state.sliceFrameHeight + state.sliceSpacing;
    if (stepX <= 0 || stepY <= 0) return false;
    return (frame.x - state.sliceMargin) % stepX == 0
        && (frame.y - state.sliceMargin) % stepY == 0;
}

std::vector<SpriteAnimationFrameDef> framesVisibleInEditorGrid(
    const SpriteAnimationClipDef& clip,
    const SpriteAnimationEditorState& state) {
    std::vector<SpriteAnimationFrameDef> frames;
    frames.reserve(clip.frames.size());
    for (const SpriteAnimationFrameDef& frame : clip.frames) {
        if (!frameMatchesEditorGrid(frame, state)) continue;
        if (std::find(frames.begin(), frames.end(), frame) == frames.end()) {
            frames.push_back(frame);
        }
    }
    return frames;
}

std::string uniqueAnimationAssetId(const ProjectDocument& document, const AssetId& imageId) {
    std::string base = imageId.empty() ? "animation" : imageId;
    std::string id = base + ".anim";
    int n = 2;
    while (document.hasSpriteAnimationAsset(id)) {
        id = base + "-" + std::to_string(n++) + ".anim";
    }
    return id;
}

std::string uniqueClipId(const SpriteAnimationAssetDef& asset) {
    int n = 1;
    while (true) {
        const std::string id = "clip-" + std::to_string(n++);
        bool exists = false;
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == id) { exists = true; break; }
        }
        if (!exists) return id;
    }
}

std::string uniqueClipName(const SpriteAnimationAssetDef& asset) {
    int n = 1;
    while (true) {
        const std::string name = "Clip " + std::to_string(n++);
        bool exists = false;
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.name == name) { exists = true; break; }
        }
        if (!exists) return name;
    }
}

std::vector<std::string> splitPipe(const std::string& value) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find('|', start);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
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
        if (action.empty()) return;

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
                } else {
                    restoreFormValue(actionElement);
                }
                event.StopPropagation();
                return;
            }
            const bool enter = type == "keydown"
                && (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER);
            if (type != "blur" && !enter) return;
        }
        if (!isCommit && type != "click" && type != "dblclick") return;

        if (type == "click" && action == "select-layer" && isLayerDoubleClick(arg)) {
            action = "begin-layer-rename";
        }

        const std::string value = formValue(actionElement, event);
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
};

// ----------------------------------------------------------------------------
EditorUi::EditorUi(EditorCoordinator& coordinator, Rml::ElementDocument* document,
                   Rml::ElementDocument* animationDocument)
    : coordinator_(coordinator),
      document_(document),
      animationDocument_(animationDocument) {}

EditorUi::~EditorUi() = default;

void EditorUi::bind() {
    if (!document_) return;
    listener_ = std::make_unique<Listener>(*this);
    const auto bindDocument = [&](Rml::ElementDocument* doc) {
        if (!doc) return;
        doc->AddEventListener("click", listener_.get());
        doc->AddEventListener("dblclick", listener_.get());
        doc->AddEventListener("blur", listener_.get(), true);
        doc->AddEventListener("keydown", listener_.get(), true);
        doc->AddEventListener("drag", listener_.get());
    };
    bindDocument(document_);
    bindDocument(animationDocument_);

    // Initial full paint of every panel.
    coordinator_.consumeInvalidations();
    applyInvalidations(EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                       | EditorInvalidation::Console  | EditorInvalidation::Toolbar
                       | EditorInvalidation::Assets);
    updateZoomReadout();   // initial paint (zoom % is Viewport-driven, not in the set)
}

void EditorUi::processFrame() {
    applyInvalidations(coordinator_.consumeInvalidations());
}

void EditorUi::applyInvalidations(EditorInvalidation flags) {
    if (flags == EditorInvalidation::None) return;
    if (has(flags, EditorInvalidation::Hierarchy) || has(flags, EditorInvalidation::Project))
        hierarchy_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Inspector))
        inspector_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Console))
        console_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Assets) || has(flags, EditorInvalidation::Project))
        assets_.refresh(document_, coordinator_);
    if (has(flags, EditorInvalidation::Viewport) || has(flags, EditorInvalidation::Assets)
        || has(flags, EditorInvalidation::Project))
        refreshSpriteAnimationEditor();
    if (has(flags, EditorInvalidation::Toolbar))
        refreshToolbar();
    if (has(flags, EditorInvalidation::Viewport))
        updateZoomReadout();
}

void EditorUi::refreshSpriteAnimationEditor() {
    if (!animationDocument_) return;
    Rml::Element* panel = animationDocument_->GetElementById("animation-editor");
    if (!panel) return;
    const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
    if (!state.openAssetId) {
        animationDocument_->Hide();
        if (!spriteAnimationEditorMarkup_.empty()) {
            spriteAnimationEditorMarkup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }
    const SpriteAnimationAssetDef* asset =
        coordinator_.document().findSpriteAnimationAsset(*state.openAssetId);
    if (!asset) {
        animationDocument_->Hide();
        if (!spriteAnimationEditorMarkup_.empty()) {
            spriteAnimationEditorMarkup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }

    animationDocument_->Show();
    animationDocument_->PullToFront();
    const SpriteAnimationClipDef* selected = selectedAnimationClip(*asset, state);
    const std::vector<SpriteAnimationFrameDef> visibleFrames =
        selected ? framesVisibleInEditorGrid(*selected, state)
                 : std::vector<SpriteAnimationFrameDef>{};
    const bool useVisibleFrames = selected && !visibleFrames.empty();
    const std::size_t displayedFrameCount =
        selected ? (useVisibleFrames ? visibleFrames.size() : selected->frames.size()) : 0;
    std::string html;
    html += "<div class=\"anim-editor-shell\">";
    html += "<div class=\"anim-editor-title\"><span>Sprite Animation Editor - "
          + escapeRml(asset->name) + "</span>"
          + "<button class=\"panel-btn\" data-action=\"close-sprite-animation\">Close</button></div>";
    html += "<div class=\"anim-editor-main\">";
    html += "<div class=\"anim-clips\"><div class=\"anim-section-title\">Clips</div>";
    for (const SpriteAnimationClipDef& clip : asset->clips) {
        std::string cls = "anim-clip";
        if (state.selectedClipId && *state.selectedClipId == clip.id) cls += " selected";
        html += "<div class=\"" + cls + "\" data-action=\"select-animation-clip\" data-arg=\""
              + escapeRml(asset->id + "|" + clip.id) + "\">" + escapeRml(clip.name) + "</div>";
    }
    if (asset->clips.empty()) html += "<div class=\"assets-empty\">No clips</div>";
    html += "<button class=\"panel-btn\" data-action=\"add-animation-clip\" data-arg=\""
          + escapeRml(asset->id) + "\"><span class=\"icon\">&#xeb0b;</span>Add Clip</button>";
    html += "</div>";

    html += "<div class=\"anim-sheet\"><div class=\"anim-sheet-head\">"
            "<div class=\"anim-sheet-title-row\"><div class=\"anim-sheet-title-block\">"
            "<div class=\"anim-section-title\">Sprite Sheet</div>"
            "<div class=\"anim-sheet-hint\">" + escapeRml(asset->imageId)
          + "  -  " + std::to_string(state.sliceFrameWidth) + "x"
          + std::to_string(state.sliceFrameHeight) + "</div></div>"
            "<button class=\"panel-btn sheet-view-reset\" data-action=\"reset-sheet-view\""
            " title=\"Reset zoom and pan\">Reset View</button></div>"
            "<div class=\"anim-slice-controls\">"
            "<div class=\"anim-tool-group atlas-grid\"><span class=\"anim-tool-label\">Atlas Grid</span>"
            "<span>Cols</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-columns\" value=\""
          + std::to_string(state.sliceColumns)
          + "\"/>"
            "<span>Rows</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-rows\" value=\""
          + std::to_string(state.sliceRows)
          + "\"/>"
            "<button class=\"panel-btn primary slice-action\" data-action=\"slice-animation-grid\""
            " title=\"Fill the selected clip from Cols x Rows\">Apply Grid</button></div>"
            "<div class=\"anim-tool-group\"><span class=\"anim-tool-label\">Cell</span>"
            "<span>W</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-frame-width\" value=\""
          + std::to_string(state.sliceFrameWidth)
          + "\"/>"
            "<span>H</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-frame-height\" value=\""
          + std::to_string(state.sliceFrameHeight)
          + "\"/></div>"
            "<div class=\"anim-tool-group\"><span class=\"anim-tool-label\">Gaps</span>"
            "<span>Margin</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-margin\" value=\""
          + std::to_string(state.sliceMargin)
          + "\"/>"
            "<span>Spacing</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-spacing\" value=\""
          + std::to_string(state.sliceSpacing)
          + "\"/></div>"
            "</div></div><div id=\"animation-sprite-canvas\""
            " class=\"anim-sprite-canvas\"></div></div>";

    html += "<div class=\"anim-settings\"><div class=\"anim-section-title\">Clip Settings</div>";
    if (selected) {
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Name</span>"
                "<input type=\"text\" class=\"prop-input\" data-action=\"commit-animation-clip-name\""
                " data-arg=\"" + escapeRml(asset->id + "|" + selected->id) + "\" value=\""
              + escapeRml(selected->name) + "\"/></div>";
        html += "<div class=\"prop-row\"><span class=\"prop-label\">FPS</span>"
                "<input type=\"text\" class=\"prop-input\" data-action=\"commit-animation-clip-fps\""
                " data-arg=\"" + escapeRml(asset->id + "|" + selected->id) + "\" value=\""
              + compactNumber(selected->framesPerSecond) + "\"/></div>";
        html += "<div class=\"mode-block\"><span class=\"mode-label\">Playback</span>"
                "<div class=\"mode-options\">";
        const auto option = [&](AnimationPlaybackMode mode, const char* label, const char* arg) {
            html += "<button class=\"panel-btn mode-option";
            if (selected->playbackMode == mode) html += " active";
            html += "\" data-action=\"set-animation-playback\" data-arg=\""
                  + escapeRml(asset->id + "|" + selected->id + "|" + arg)
                  + "\">" + label + "</button>";
        };
        option(AnimationPlaybackMode::Loop, "Loop", "loop");
        option(AnimationPlaybackMode::Once, "Once", "once");
        html += "</div></div>";
        html += "<div class=\"prop-row\"><span class=\"prop-label\">Frames</span>"
                "<span class=\"prop-readonly\">" + std::to_string(displayedFrameCount)
              + "</span></div>";
        html += "<div class=\"anim-section-title anim-preview-title\">Preview</div>"
                "<div id=\"animation-preview-canvas\" class=\"anim-preview-canvas\"></div>"
                "<div class=\"anim-settings-actions\">";
        // Static markup on purpose: the play state is reflected by the "active"
        // class set below, so a Once clip finishing mid-typing never triggers a
        // SetInnerRML rebuild (which would steal focus from Name/FPS inputs).
        html += "<button id=\"anim-preview-toggle\" class=\"panel-btn anim-preview-toggle\""
                " data-action=\"toggle-animation-preview\">"
                "<span class=\"icon preview-when-stopped\">&#xed46;</span>"
                "<span class=\"icon preview-when-playing\">&#xed45;</span>"
                "<span class=\"preview-when-stopped\">Play</span>"
                "<span class=\"preview-when-playing\">Pause</span>"
                "</button>";
        html += "<button class=\"panel-btn\" data-action=\"remove-animation-clip\" data-arg=\""
              + escapeRml(asset->id + "|" + selected->id)
              + "\"><span class=\"icon\">&#xeb41;</span>Remove Clip</button></div>";
    } else {
        html += "<div class=\"assets-empty\">Select or add a clip</div>";
    }
    html += "</div></div>";

    html += "<div class=\"anim-timeline\"><div class=\"anim-section-title\">Timeline</div>";
    if (selected && displayedFrameCount > 0) {
        for (std::size_t i = 0; i < displayedFrameCount; ++i) {
            html += "<span class=\"anim-frame\">Frame " + std::to_string(i + 1) + "</span>";
        }
    } else {
        html += "<span class=\"assets-empty\">No frames</span>";
    }
    html += "</div></div>";
    if (html != spriteAnimationEditorMarkup_) {
        spriteAnimationEditorMarkup_ = html;
        panel->SetInnerRML(html);
    }
    // Play state lives outside the generated markup (see the toggle button):
    // toggled here on the live element, never via a panel rebuild.
    if (Rml::Element* toggle = animationDocument_->GetElementById("anim-preview-toggle")) {
        toggle->SetClass("active", state.previewPlaying);
    }
}

void EditorUi::updateZoomReadout() {
    if (!document_) return;
    Rml::Element* el = document_->GetElementById("toolbar-zoom");
    if (!el) return;
    const SceneId active = (coordinator_.isPlaying() && coordinator_.playSession())
        ? coordinator_.playSession()->sceneId()
        : coordinator_.state().activeSceneId;
    const int pct = static_cast<int>(coordinator_.sceneView(active).zoom * 100.f + 0.5f);
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

void EditorUi::setFitViewHandler(WorkspaceRequest fitView) {
    fitViewRequest_ = std::move(fitView);
}

void EditorUi::setAnimationSliceHandler(WorkspaceRequest sliceAnimation) {
    sliceAnimationRequest_ = std::move(sliceAnimation);
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

void EditorUi::hideViewportContextMenu() {
    if (!document_) return;
    if (Rml::Element* menu = document_->GetElementById("viewport-context-menu")) {
        menu->SetClass("hidden", true);
    }
    viewportContextMenuVisible_ = false;
}

bool EditorUi::isViewportContextMenuHit(int physicalX, int physicalY) const {
    if (!document_ || !viewportContextMenuVisible_) return false;
    Rml::Element* menu = document_->GetElementById("viewport-context-menu");
    if (!menu) return false;
    const Rml::Vector2f offset = menu->GetAbsoluteOffset();
    const float left = offset.x;
    const float top = offset.y;
    const float right = left + menu->GetClientWidth();
    const float bottom = top + menu->GetClientHeight();
    const float x = static_cast<float>(physicalX);
    const float y = static_cast<float>(physicalY);
    return x >= left && x < right && y >= top && y < bottom;
}

void EditorUi::refreshToolbar() {
    if (!document_) return;
    const bool playing = coordinator_.isPlaying();

    if (Rml::Element* status = document_->GetElementById("toolbar-status")) {
        std::string text;
        if (playing && coordinator_.playSession()) {
            text = "PLAYING - " + coordinator_.playSession()->scene().name;
        } else {
            const SceneDef* scene =
                coordinator_.document().findScene(coordinator_.state().activeSceneId);
            text = (scene ? scene->name : std::string("-")) + "  -  EDIT";
        }
        status->SetInnerRML(escapeRml(text));
    }

    // Play affordances derive straight from the authorities — never stored.
    const auto setEnabled = [&](const char* id, bool enabled) {
        if (Rml::Element* el = document_->GetElementById(id))
            el->SetClass("disabled", !enabled);
    };
    setEnabled("btn-play-project", !playing && coordinator_.canPlayProject());
    setEnabled("btn-play-scene",   !playing && coordinator_.canPlayCurrentScene());
    setEnabled("btn-stop",         playing);
    // Undo/Redo are derived affordances: available only with history and outside Play.
    setEnabled("btn-undo",         !playing && coordinator_.canUndo());
    setEnabled("btn-redo",         !playing && coordinator_.canRedo());
    setEnabled("btn-grid-visible", !playing);
    setEnabled("btn-grid-snap",    !playing);
    setEnabled("btn-grid-size",    !playing);

    const SceneId active = (playing && coordinator_.playSession())
        ? coordinator_.playSession()->sceneId()
        : coordinator_.state().activeSceneId;
    const EditorSceneViewState& view = coordinator_.sceneView(active);
    if (Rml::Element* el = document_->GetElementById("btn-grid-visible"))
        el->SetClass("active", view.gridVisible && !playing);
    if (Rml::Element* el = document_->GetElementById("btn-grid-snap"))
        el->SetClass("active", view.gridSnapEnabled && !playing);
    const std::string cellSize = compactNumber(view.gridCellSize);
    if (Rml::Element* el = document_->GetElementById("btn-grid-size"))
        el->SetInnerRML(escapeRml(cellSize) + " &#x25be;");
    if (Rml::Element* el = document_->GetElementById("grid-size-control"))
        el->SetClass("disabled", playing);
    if (Rml::Element* el = document_->GetElementById("grid-cell-size-input")) {
        el->SetAttribute("value", cellSize);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(el))
            control->SetValue(cellSize);
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

void EditorUi::commitGridCellSize(const std::string& text) {
    if (coordinator_.isPlaying()) return;
    const std::optional<float> parsed = parseNumberField(text);
    if (!parsed.has_value()) return;
    coordinator_.apply(SetSceneGridCellSizeIntent{coordinator_.state().activeSceneId, *parsed});
}

void EditorUi::handleAction(const std::string& action, const std::string& arg,
                            const std::string& value) {
    const EntityId selected = coordinator_.selection().primaryEntity;

    // Inspector Add Component menu: toggle it open/closed, and close it whenever a
    // component is actually added (the add invalidates the Inspector, which then
    // re-renders without the menu). The coordinator still guards the commands.
    if (action == "toggle-add-component") {
        if (!coordinator_.isPlaying()) inspector_.toggleAddMenu(document_, coordinator_);
        return;
    }
    if (action == "add-sprite-renderer" || action == "add-box-collider"
        || action == "add-linear-mover" || action == "add-top-down"
        || action == "add-platformer") {
        inspector_.closeAddMenu();   // then fall through to execute the add
    }

    if (action == "select-entity") {
        coordinator_.apply(SelectEntityIntent{
            static_cast<EntityId>(std::strtoul(arg.c_str(), nullptr, 10))});
    } else if (action == "select-scene") {
        coordinator_.apply(SelectSceneIntent{arg});
    } else if (action == "add-scene") {
        addScene(coordinator_);
    } else if (action == "delete-scene") {
        // No arg → the active scene; the coordinator reconciles the workspace.
        deleteScene(coordinator_, arg.empty() ? coordinator_.state().activeSceneId : arg);
    } else if (action == "add-entity") {
        if (addEntityRequest_) addEntityRequest_();
        else addEntity(coordinator_);
    } else if (action == "add-instance") {
        if (addInstanceRequest_) addInstanceRequest_();
        else addInstanceOfSelectedType(coordinator_);
    } else if (action == "select-layer") {
        coordinator_.apply(SetActiveLayerIntent{coordinator_.state().activeSceneId, arg});
    } else if (action == "toggle-layer-visible") {
        coordinator_.apply(
            ToggleLayerEditorVisibilityIntent{coordinator_.state().activeSceneId, arg});
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
        hideViewportContextMenu();
        if (createEntityHereRequest_) createEntityHereRequest_();
    } else if (action == "create-instance-here") {
        hideViewportContextMenu();
        if (createInstanceHereRequest_) createInstanceHereRequest_();
    } else if (action == "delete-entity") {
        deleteSelectedEntity(coordinator_);
    } else if (action == "set-start-scene") {
        setStartScene(coordinator_, arg.empty() ? coordinator_.state().activeSceneId : arg);
    } else if (action == "add-sprite-renderer") {
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
        if (!coordinator_.isPlaying()
            && setSpriteRendererAnimation(coordinator_, arg).ok) {
            coordinator_.apply(OpenSpriteAnimationEditorIntent{arg});
        }
    } else if (action == "commit-animator-speed") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value() && selected != INVALID_ENTITY) {
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
    } else if (action == "create-sprite-animation") {
        if (!coordinator_.isPlaying() && coordinator_.document().hasImageAsset(arg)) {
            const std::string id = uniqueAnimationAssetId(coordinator_.document(), arg);
            const std::string name = id;
            if (coordinator_.execute(AddSpriteAnimationAssetCommand{id, arg, name}).ok) {
                coordinator_.apply(OpenSpriteAnimationEditorIntent{id});
            }
        }
    } else if (action == "open-sprite-animation") {
        if (!coordinator_.isPlaying()) coordinator_.apply(OpenSpriteAnimationEditorIntent{arg});
    } else if (action == "close-sprite-animation") {
        coordinator_.apply(CloseSpriteAnimationEditorIntent{});
    } else if (action == "remove-sprite-animation") {
        if (!arg.empty()) coordinator_.execute(RemoveSpriteAnimationAssetCommand{arg});
    } else if (action == "select-animation-clip") {
        const std::vector<std::string> parts = splitPipe(arg);
        if (parts.size() == 2) coordinator_.apply(SelectAnimationClipIntent{parts[0], parts[1]});
    } else if (action == "add-animation-clip") {
        const SpriteAnimationAssetDef* asset =
            coordinator_.document().findSpriteAnimationAsset(arg);
        if (asset) {
            const std::string clipId = uniqueClipId(*asset);
            if (coordinator_.execute(AddAnimationClipCommand{
                    arg, clipId, uniqueClipName(*asset)}).ok) {
                coordinator_.apply(SelectAnimationClipIntent{arg, clipId});
            }
        }
    } else if (action == "commit-animation-clip-name") {
        const std::vector<std::string> parts = splitPipe(arg);
        if (parts.size() == 2 && !value.empty()) {
            coordinator_.execute(RenameAnimationClipCommand{parts[0], parts[1], value});
        }
    } else if (action == "commit-animation-clip-fps") {
        const std::vector<std::string> parts = splitPipe(arg);
        const std::optional<float> parsed = parseNumberField(value);
        if (parts.size() == 2 && parsed.has_value()) {
            coordinator_.execute(SetAnimationClipFrameRateCommand{parts[0], parts[1], *parsed});
        }
    } else if (action == "commit-animation-frame-width"
               || action == "commit-animation-frame-height"
               || action == "commit-animation-margin"
               || action == "commit-animation-spacing") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value()) {
            const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
            SetAnimationSlicingIntent intent{
                state.sliceFrameWidth,
                state.sliceFrameHeight,
                state.sliceMargin,
                state.sliceSpacing,
            };
            const int rounded = static_cast<int>(std::round(*parsed));
            if (action == "commit-animation-frame-width") intent.frameWidth = rounded;
            else if (action == "commit-animation-frame-height") intent.frameHeight = rounded;
            else if (action == "commit-animation-margin") intent.margin = rounded;
            else intent.spacing = rounded;
            coordinator_.apply(intent);
        }
    } else if (action == "commit-animation-columns" || action == "commit-animation-rows") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value()) {
            const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
            SetAnimationSliceGridIntent intent{state.sliceColumns, state.sliceRows};
            const int rounded = static_cast<int>(std::round(*parsed));
            if (action == "commit-animation-columns") intent.columns = rounded;
            else intent.rows = rounded;
            coordinator_.apply(intent);
        }
    } else if (action == "slice-animation-grid") {
        if (sliceAnimationRequest_) sliceAnimationRequest_();
    } else if (action == "toggle-animation-preview") {
        coordinator_.apply(SetAnimationPreviewPlayingIntent{
            !coordinator_.state().spriteAnimationEditor.previewPlaying});
    } else if (action == "reset-sheet-view") {
        const Vec2 pan = coordinator_.state().spriteAnimationEditor.sheetPan;
        coordinator_.apply(SetSpriteSheetZoomIntent{1.f});
        coordinator_.apply(PanSpriteSheetIntent{{-pan.x, -pan.y}});
    } else if (action == "set-animation-playback") {
        const std::vector<std::string> parts = splitPipe(arg);
        if (parts.size() == 3) {
            const AnimationPlaybackMode mode =
                parts[2] == "once" ? AnimationPlaybackMode::Once : AnimationPlaybackMode::Loop;
            coordinator_.execute(SetAnimationClipPlaybackModeCommand{parts[0], parts[1], mode});
        }
    } else if (action == "remove-animation-clip") {
        const std::vector<std::string> parts = splitPipe(arg);
        if (parts.size() == 2) coordinator_.execute(RemoveAnimationClipCommand{parts[0], parts[1]});
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
        if (coordinator_.isPlaying()) return;
        const std::optional<BoxColliderMode> mode = parseBoxColliderModeArg(arg);
        if (mode.has_value()) setBoxColliderMode(coordinator_, *mode);
    } else if (action == "commit-box-offset-x" || action == "commit-box-offset-y"
               || action == "commit-box-size-x" || action == "commit-box-size-y") {
        const SceneInstanceDef* inst = coordinator_.document().findInstanceInScene(
            coordinator_.state().activeSceneId, coordinator_.selection().primaryEntity);
        if (inst) {
            const auto& types = coordinator_.document().data().objectTypes;
            const auto typeIt = types.find(inst->objectTypeId);
            if (typeIt != types.end() && typeIt->second.boxCollider2D) {
                const BoxCollider2DComponent& collider = *typeIt->second.boxCollider2D;
                const std::optional<float> parsed = parseNumberField(value);
                if (!parsed.has_value()) return;
                if (action == "commit-box-offset-x")
                    setBoxColliderOffset(coordinator_, Vec2{*parsed, collider.offset.y});
                else if (action == "commit-box-offset-y")
                    setBoxColliderOffset(coordinator_, Vec2{collider.offset.x, *parsed});
                else if (action == "commit-box-size-x")
                    setBoxColliderSize(coordinator_, Vec2{*parsed, collider.size.y});
                else
                    setBoxColliderSize(coordinator_, Vec2{collider.size.x, *parsed});
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
        if (inst) {
            const auto& types = coordinator_.document().data().objectTypes;
            const auto typeIt = types.find(inst->objectTypeId);
            if (typeIt != types.end() && typeIt->second.linearMover) {
                const LinearMoverComponent& m = *typeIt->second.linearMover;
                const std::optional<float> parsed = parseNumberField(value);
                if (!parsed.has_value()) return;
                if (action == "commit-mover-dir-x")
                    setLinearMoverDirection(coordinator_, Vec2{*parsed, m.directionY});
                else if (action == "commit-mover-dir-y")
                    setLinearMoverDirection(coordinator_, Vec2{m.directionX, *parsed});
                else
                    setLinearMoverSpeed(coordinator_, *parsed);
            }
        }
    } else if (action == "add-top-down") {
        addTopDownController(coordinator_);
    } else if (action == "remove-top-down") {
        removeTopDownController(coordinator_);
    } else if (action == "commit-topdown-speed") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value()) setTopDownControllerSpeed(coordinator_, *parsed);
    } else if (action == "add-platformer") {
        addPlatformerController(coordinator_);
    } else if (action == "remove-platformer") {
        removePlatformerController(coordinator_);
    } else if (action == "commit-platformer-move") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value()) setPlatformerMoveSpeed(coordinator_, *parsed);
    } else if (action == "commit-platformer-jump") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value()) setPlatformerJumpSpeed(coordinator_, *parsed);
    } else if (action == "commit-platformer-gravity") {
        const std::optional<float> parsed = parseNumberField(value);
        if (parsed.has_value()) setPlatformerGravity(coordinator_, *parsed);
    } else if (action == "commit-pos-x") {
        commitInspectorPositionX(coordinator_, selected, value);
    } else if (action == "commit-pos-y") {
        commitInspectorPositionY(coordinator_, selected, value);
    } else if (action == "commit-name") {
        if (selected != INVALID_ENTITY && !value.empty())
            coordinator_.execute(
                RenameEntityCommand{coordinator_.state().activeSceneId, selected, value});
    } else if (action == "commit-scene-name") {
        if (!value.empty())
            coordinator_.execute(RenameSceneCommand{coordinator_.state().activeSceneId, value});
    } else if (action == "commit-project-name") {
        if (!value.empty()) coordinator_.execute(RenameProjectCommand{value});
    } else if (action == "fit-view-to-bounds") {
        if (fitViewRequest_) fitViewRequest_();   // workspace-only (camera), no command
    } else if (action == "commit-scene-width" || action == "commit-scene-height") {
        const SceneDef* scene =
            coordinator_.document().findScene(coordinator_.state().activeSceneId);
        const std::optional<float> parsed = parseNumberField(value);
        if (scene && parsed.has_value()) {
            Vec2 size = scene->worldSize;
            if (action == "commit-scene-width") size.x = *parsed;
            else                                size.y = *parsed;
            coordinator_.execute(SetSceneSizeCommand{coordinator_.state().activeSceneId, size});
        }
    } else if (action == "undo") {
        coordinator_.undo();
    } else if (action == "redo") {
        coordinator_.redo();
    } else if (action == "reset-zoom") {
        const SceneId active = (coordinator_.isPlaying() && coordinator_.playSession())
            ? coordinator_.playSession()->sceneId()
            : coordinator_.state().activeSceneId;
        coordinator_.apply(SetViewportZoomIntent{active, 1.0f});   // target unchanged
    } else if (action == "toggle-grid-visible") {
        if (!coordinator_.isPlaying()) {
            const SceneId active = coordinator_.state().activeSceneId;
            coordinator_.apply(SetSceneGridVisibilityIntent{
                active, !coordinator_.sceneView(active).gridVisible});
        }
    } else if (action == "toggle-grid-snap") {
        if (!coordinator_.isPlaying()) {
            const SceneId active = coordinator_.state().activeSceneId;
            coordinator_.apply(SetSceneGridSnapEnabledIntent{
                active, !coordinator_.sceneView(active).gridSnapEnabled});
        }
    } else if (action == "commit-grid-cell-size") {
        commitGridCellSize(value);
    } else if (action == "set-grid-cell-size") {
        commitGridCellSize(arg);
    } else if (action == "zoom-in" || action == "zoom-out") {
        const SceneId active = coordinator_.state().activeSceneId;
        const float current = coordinator_.sceneView(active).zoom;
        const float factor = (action == "zoom-in") ? 1.2f : (1.0f / 1.2f);
        coordinator_.apply(SetViewportZoomIntent{active, current * factor});
    } else if (action == "play-project") {
        coordinator_.playProject();        // guarded; no-op without a valid start scene
    } else if (action == "play-current-scene") {
        coordinator_.playCurrentScene();   // guarded; no-op without an active scene
    } else if (action == "stop") {
        coordinator_.stopPlaying();
    } else if (action == "import-image") {
        if (importAssetRequest_) importAssetRequest_(AssetKind::Image);
    } else if (action == "import-audio") {
        if (importAssetRequest_) importAssetRequest_(AssetKind::Audio);
    } else if (action == "import-font") {
        if (importAssetRequest_) importAssetRequest_(AssetKind::Font);
    } else if (action == "remove-image-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveImageAssetCommand{arg});
    } else if (action == "remove-audio-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveAudioAssetCommand{arg});
    } else if (action == "remove-font-asset") {
        if (!arg.empty()) coordinator_.execute(RemoveFontAssetCommand{arg});
    } else if (action == "new-project") {
        if (newProjectRequest_) newProjectRequest_();
    } else if (action == "select-console") {
        console_.select(static_cast<std::size_t>(std::strtoul(arg.c_str(), nullptr, 10)),
                        document_, coordinator_);
    } else if (action == "copy-console") {
        copySelectedConsoleMessage();
    } else if (action == "open-project") {
        if (openProjectRequest_) openProjectRequest_();
    } else if (action == "save-project") {
        if (saveProjectRequest_) saveProjectRequest_();
    } else if (action == "save-project-as") {
        if (saveProjectAsRequest_) saveProjectAsRequest_();
    }
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
    } else if (action == "resize-right") {
        coordinator_.apply(ResizePanelIntent{ResizePanelIntent::Panel::Right,
                                             static_cast<float>(dims.x) - mouseX});
        if (Rml::Element* el = document_->GetElementById("right-col"))
            el->SetProperty("width", px(coordinator_.uiState().rightPanelWidth));
    } else if (action == "resize-console") {
        coordinator_.apply(ResizePanelIntent{ResizePanelIntent::Panel::Console,
                                             static_cast<float>(dims.y) - mouseY});
        if (Rml::Element* el = document_->GetElementById("console"))
            el->SetProperty("height", px(coordinator_.uiState().consoleHeight));
    }
}

} // namespace ArtCade::EditorNative
