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

std::string uniqueTilesetAssetId(const ProjectDocument& document, const AssetId& imageId) {
    std::string base = imageId.empty() ? "tileset" : imageId;
    std::string id = base + ".tileset";
    int n = 2;
    while (document.hasTilesetAsset(id)) {
        id = base + "-" + std::to_string(n++) + ".tileset";
    }
    return id;
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
                   Rml::ElementDocument* animationDocument,
                   Rml::ElementDocument* tilesetDocument)
    : coordinator_(coordinator),
      document_(document),
      animationDocument_(animationDocument),
      tilesetDocument_(tilesetDocument) {}

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
    bindDocument(tilesetDocument_);

    // Initial full paint of every panel.
    coordinator_.consumeInvalidations();
    applyInvalidations(EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                       | EditorInvalidation::Console  | EditorInvalidation::Toolbar
                       | EditorInvalidation::Assets   | EditorInvalidation::Layout);
    updateZoomReadout();   // initial paint (zoom % is Viewport-driven, not in the set)
}

void EditorUi::processFrame() {
    applyInvalidations(coordinator_.consumeInvalidations());
    // Deferred hierarchy context menu: shown here, after the application's
    // outside-click check for this frame has already run.
    showPendingHierarchyMenu();
    // The preview playhead advances without invalidation (workspace tick), so
    // the timeline highlight and Play/Pause affordance follow it live here,
    // class-only — never via a markup rebuild that would steal input focus.
    updateSpriteAnimationPlayhead();
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
        || has(flags, EditorInvalidation::Project)) {
        refreshSpriteAnimationEditor();
        refreshTilesetEditor();
    }
    if (has(flags, EditorInvalidation::Toolbar))
        refreshToolbar();
    if (has(flags, EditorInvalidation::Viewport))
        updateZoomReadout();
    if (has(flags, EditorInvalidation::Layout))
        refreshLayout();
}

void EditorUi::refreshLayout() {
    if (!document_) return;
    const bool consoleVisible = coordinator_.uiState().consoleVisible;
    if (Rml::Element* console = document_->GetElementById("console"))
        console->SetClass("hidden", !consoleVisible);
    if (Rml::Element* splitter = document_->GetElementById("split-console"))
        splitter->SetClass("hidden", !consoleVisible);
}

void EditorUi::refreshSpriteAnimationEditor() {
    if (!animationDocument_) return;
    Rml::Element* panel = animationDocument_->GetElementById("animation-editor");
    if (!panel) return;
    const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
    if (!state.openAssetId) {
        animationDocument_->Hide();
        spriteAnimationTimelineCount_ = 0;
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
        spriteAnimationTimelineCount_ = 0;
        if (!spriteAnimationEditorMarkup_.empty()) {
            spriteAnimationEditorMarkup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }

    animationDocument_->Show();
    animationDocument_->PullToFront();
    const SpriteAnimationClipDef* selected = selectedAnimationClip(*asset, state);
    // The clip's frames are the sequence, shown 1:1 in the timeline.
    const std::size_t displayedFrameCount = selected ? selected->frames.size() : 0;
    std::string html;
    html += "<div class=\"anim-editor-shell\">";
    html += "<div class=\"anim-editor-title\">"
            "<span class=\"anim-editor-eyebrow\">Sprite Animation Editor</span>"
            "<span class=\"anim-editor-name\">" + escapeRml(asset->name) + "</span>"
            "<button class=\"panel-btn\" data-action=\"close-sprite-animation\">Close</button></div>";
    html += "<div class=\"anim-editor-main\">";

    // -- Left column: Animations (asset switcher) over Clips ------------------
    // The editor edits one animation asset at a time; this list shows every
    // animation in the project so a new one (Import Sheet) is clearly an add, not
    // a replace, and all of them stay reachable after a reload. Each row reuses
    // the same open-sprite-animation entry point as the Assets panel.
    html += "<div class=\"anim-clips\"><div class=\"anim-panel-title\">Animations</div>"
            "<div class=\"anim-asset-list\">";
    for (const SpriteAnimationAssetDef& a : coordinator_.document().data().spriteAnimationAssets) {
        std::string cls = "anim-asset";
        if (a.id == asset->id) cls += " selected";
        html += "<div class=\"" + cls + "\" data-action=\"open-sprite-animation\" data-arg=\""
              + escapeRml(a.id) + "\">"
                "<span class=\"anim-asset-name\">" + escapeRml(a.name) + "</span>"
                "<span class=\"anim-clip-count\">" + std::to_string(a.clips.size())
              + "</span>"
                // Same remove-sprite-animation entry point the Assets panel's trash
                // icon uses; nested inside a row with its own data-action, but the
                // listener resolves the nearest ancestor's data-action first, so a
                // click here never also triggers open-sprite-animation.
                "<button class=\"anim-asset-remove\" data-action=\"remove-sprite-animation\""
                " data-arg=\"" + escapeRml(a.id) + "\" title=\"Delete this animation\">"
                "<span class=\"icon\">&#xeb41;</span></button>"
                "</div>";
    }
    html += "</div><button class=\"anim-import-sheet\" data-action=\"import-animation-sheet\""
            " title=\"Import an image and start a new animation on it\">"
            "<span class=\"icon\">&#xea7a;</span>Import Sheet</button>";

    html += "<div class=\"anim-panel-title anim-section-gap\">Clips</div>"
            "<div class=\"anim-clip-list\">";
    for (const SpriteAnimationClipDef& clip : asset->clips) {
        std::string cls = "anim-clip";
        if (state.selectedClipId && *state.selectedClipId == clip.id) cls += " selected";
        html += "<div class=\"" + cls + "\" data-action=\"select-animation-clip\" data-arg=\""
              + escapeRml(asset->id + "|" + clip.id) + "\">"
                "<span class=\"anim-clip-name\">" + escapeRml(clip.name) + "</span>"
                "<span class=\"anim-clip-count\">" + std::to_string(clip.frames.size())
              + "</span></div>";
    }
    if (asset->clips.empty()) html += "<div class=\"assets-empty\">No clips yet</div>";
    html += "</div><button class=\"anim-add-clip\" data-action=\"add-animation-clip\""
            " data-arg=\"" + escapeRml(asset->id)
          + "\" title=\"Add another clip on this same sheet\">"
            "<span class=\"icon\">&#xeb0b;</span>Add Clip</button></div>";

    // -- Sheet column -----------------------------------------------------------
    html += "<div class=\"anim-sheet\"><div class=\"anim-sheet-head\">"
            "<div class=\"anim-panel-title\">Sprite Sheet</div>"
            "<span class=\"anim-sheet-meta\">"
          + escapeRml(editorSheetImageId(*asset, state.selectedClipId))
          + "</span>"
            "<button class=\"panel-btn sheet-view-reset\" data-action=\"reset-sheet-view\""
            " title=\"Reset zoom and pan\">Reset View</button></div>"
            "<div class=\"anim-slice-controls\">"
            "<div class=\"anim-tool-group\"><span class=\"anim-tool-label\">Frames</span>"
            "<span class=\"anim-tool-field\">Cols</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-columns\" value=\""
          + std::to_string(state.sliceColumns)
          + "\"/>"
            "<span class=\"anim-tool-field\">Rows</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-rows\" value=\""
          + std::to_string(state.sliceRows)
          + "\"/></div>"
            "<div class=\"anim-tool-group\"><span class=\"anim-tool-label\">Gaps</span>"
            "<span class=\"anim-tool-field\">Margin</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-margin\" value=\""
          + std::to_string(state.sliceMargin)
          + "\"/>"
            "<span class=\"anim-tool-field\">Spacing</span><input type=\"text\" class=\"prop-input compact\""
            " data-action=\"commit-animation-spacing\" value=\""
          + std::to_string(state.sliceSpacing)
          + "\"/></div>"
            "<button class=\"panel-btn primary slice-action\" data-action=\"slice-animation-grid\""
            " title=\"Divide the sheet into Cols x Rows frames, in reading order\">"
            "Slice into Frames</button>"
            "</div>";
    if (asset->clips.empty()) {
        // Empty state sits where the user is looking (above the sheet), with
        // the primary action inline; the sidebar Add Clip stays as secondary
        // entry. Both converge on the same add-animation-clip action. In-flow
        // on purpose: raylib paints the sheet after RmlUi, so an overlay inside
        // the canvas rect would be drawn over.
        html += "<div class=\"anim-canvas-empty\">"
                "<div class=\"anim-canvas-empty-text\">"
                "<div class=\"anim-canvas-empty-title\">Ready to slice</div>"
                "<div class=\"anim-canvas-empty-hint\">Set how many frames the sheet holds"
                " and click Slice into Frames - the clip is created for you.</div></div>"
                "<button class=\"anim-canvas-empty-cta\" data-action=\"slice-animation-grid\""
                " title=\"Create a clip and fill it from the grid in one step\">"
                "Slice into Frames</button></div>";
    }
    html += "<div id=\"animation-sprite-canvas\" class=\"anim-sprite-canvas\"></div>"
            "<div class=\"anim-sheet-footer\">Click a cell to add or remove it"
            "&nbsp;&nbsp;&#183;&nbsp;&nbsp;Wheel to zoom"
            "&nbsp;&nbsp;&#183;&nbsp;&nbsp;Middle mouse or Space + drag to pan</div></div>";

    // -- Clip settings column ----------------------------------------------------
    html += "<div class=\"anim-settings\"><div class=\"anim-panel-title\">Clip Settings</div>";
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
        // Static markup on purpose: play state is reflected through the "active"
        // class in updateSpriteAnimationPlayhead(), so a Once clip finishing
        // mid-typing never triggers a SetInnerRML rebuild (focus steal).
        html += "<div class=\"anim-panel-title anim-preview-title\">Preview</div>"
                "<div id=\"animation-preview-canvas\" class=\"anim-preview-canvas\"></div>"
                "<div class=\"anim-transport\">"
                "<button class=\"panel-btn\" data-action=\"step-animation-preview\""
                " data-arg=\"-1\" title=\"Previous frame\"><span class=\"icon\">&#xed48;</span></button>"
                "<button id=\"anim-preview-toggle\" class=\"panel-btn anim-preview-toggle\""
                " data-action=\"toggle-animation-preview\" title=\"Play / Pause\">"
                "<span class=\"icon preview-when-stopped\">&#xed46;</span>"
                "<span class=\"icon preview-when-playing\">&#xed45;</span></button>"
                "<button class=\"panel-btn\" data-action=\"step-animation-preview\""
                " data-arg=\"1\" title=\"Next frame\"><span class=\"icon\">&#xed49;</span></button>"
                "</div>";
        html += "<div class=\"anim-settings-spacer\"></div>"
                "<button class=\"anim-remove-clip\" data-action=\"remove-animation-clip\" data-arg=\""
              + escapeRml(asset->id + "|" + selected->id)
              + "\"><span class=\"icon\">&#xeb41;</span>Remove Clip</button>";
    } else {
        html += "<div class=\"assets-empty\">Select or add a clip</div>";
    }
    html += "</div></div>";

    // -- Timeline strip -----------------------------------------------------------
    html += "<div class=\"anim-timeline\"><div class=\"anim-timeline-head\">"
            "<div class=\"anim-panel-title\">Timeline</div>";
    if (selected && displayedFrameCount > 0) {
        html += "<span class=\"anim-timeline-readout\">"
              + std::to_string(displayedFrameCount) + " frames &#183; "
              + compactNumber(selected->framesPerSecond) + " fps</span>"
                "<button class=\"panel-btn\" data-action=\"clear-animation-frames\" data-arg=\""
              + escapeRml(asset->id + "|" + selected->id)
              + "\" title=\"Remove every frame from the clip\">Clear</button>";
    }
    html += "</div><div class=\"anim-timeline-track\">";
    if (selected && displayedFrameCount > 0) {
        for (std::size_t i = 0; i < displayedFrameCount; ++i) {
            const std::string index = std::to_string(i);
            // Head strip carries the order + remove (RmlUi, clickable); the thumb
            // sub-div below is the rect raylib paints the frame image into, so the
            // controls are never covered by the raylib overlay.
            html += "<div id=\"anim-frame-chip-" + index + "\" class=\"anim-frame\""
                    " data-action=\"set-animation-preview-frame\" data-arg=\"" + index
                  + "\" title=\"Show this frame in the preview\">"
                    "<div class=\"anim-frame-head\">"
                    "<span class=\"anim-frame-order\">" + std::to_string(i + 1) + "</span>"
                    "<span class=\"anim-frame-remove\" data-action=\"remove-animation-frame\""
                    " data-arg=\"" + index + "\" title=\"Remove this frame\">&#xd7;</span>"
                    "</div>"
                    "<div id=\"anim-frame-thumb-" + index + "\" class=\"anim-frame-thumb\"></div>"
                    "</div>";
        }
    } else {
        html += "<span class=\"assets-empty\">No frames - set Cols x Rows and click"
                " Slice into Frames, or click cells on the sheet</span>";
    }
    html += "</div></div></div>";
    if (html != spriteAnimationEditorMarkup_) {
        spriteAnimationEditorMarkup_ = html;
        panel->SetInnerRML(html);
    }
    // Playhead highlight + play state live outside the generated markup; they are
    // applied per frame in updateSpriteAnimationPlayhead() via classes only.
    spriteAnimationTimelineCount_ = displayedFrameCount;
}

void EditorUi::refreshTilesetEditor() {
    if (!tilesetDocument_) return;
    Rml::Element* panel = tilesetDocument_->GetElementById("tileset-editor");
    if (!panel) return;
    const TilesetEditorState& state = coordinator_.state().tilesetEditor;
    const TilesetAsset* asset =
        state.openAssetId ? coordinator_.document().findTilesetAsset(*state.openAssetId) : nullptr;
    if (!asset) {
        tilesetDocument_->Hide();
        if (!tilesetEditorMarkup_.empty()) {
            tilesetEditorMarkup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }

    tilesetDocument_->Show();
    tilesetDocument_->PullToFront();
    const TilesetSlicing& s = state.pendingSlicing;
    const auto sliceField = [](const char* label, const char* action, int value) {
        return std::string("<div class=\"tileset-slice-row\"><span class=\"prop-label\">") + label
             + "</span><input type=\"text\" class=\"prop-input\" data-action=\"" + action
             + "\" value=\"" + std::to_string(value) + "\"/></div>";
    };
    std::string html;
    html += "<div class=\"tileset-editor-shell\">";
    html += "<div class=\"tileset-editor-title\">"
            "<span class=\"tileset-editor-eyebrow\">Tileset Editor</span>"
            "<input type=\"text\" class=\"tileset-editor-name\" data-action=\"commit-tileset-name\""
            " data-arg=\"" + escapeRml(asset->assetId) + "\" value=\"" + escapeRml(asset->name) + "\"/>"
            "<button id=\"tileset-close-btn\" class=\"panel-btn\""
            " data-action=\"close-tileset-editor\">Close</button></div>";
    html += "<div class=\"tileset-editor-main\">";

    html += "<div class=\"tileset-canvas-col\"><div class=\"tileset-panel-title\">Source Image</div>"
            "<div id=\"tileset-canvas\"></div>"
            "<div class=\"tileset-canvas-footer\">Wheel to zoom"
            "&nbsp;&nbsp;&#183;&nbsp;&nbsp;Middle mouse or Space + drag to pan"
            "&nbsp;&nbsp;&#183;&nbsp;&nbsp;Click a tile to select it</div></div>";

    html += "<div class=\"tileset-settings\"><div class=\"tileset-panel-title\">Slicing</div>";
    html += sliceField("Tile Width", "commit-tileset-tile-width", s.tileWidth);
    html += sliceField("Tile Height", "commit-tileset-tile-height", s.tileHeight);
    html += sliceField("Margin X", "commit-tileset-margin-x", s.marginX);
    html += sliceField("Margin Y", "commit-tileset-margin-y", s.marginY);
    html += sliceField("Spacing X", "commit-tileset-spacing-x", s.spacingX);
    html += sliceField("Spacing Y", "commit-tileset-spacing-y", s.spacingY);
    html += "<div class=\"tileset-settings-actions\">"
            "<button id=\"tileset-apply-btn\" class=\"panel-btn primary\""
            " data-action=\"apply-tileset-slicing\""
            " title=\"Commit this slicing to the tileset\">Apply</button>"
            "<button id=\"tileset-cancel-btn\" class=\"panel-btn\" data-action=\"close-tileset-editor\""
            " title=\"Discard changes\">Cancel</button></div>";
    html += "<span class=\"tileset-settings-diagnostic\">Source: " + escapeRml(asset->imageAssetId)
          + "<br/>" + std::to_string(asset->tiles.size()) + " tile(s) committed</span>";
    html += "</div>";      // .tileset-settings
    html += "</div></div>"; // .tileset-editor-main, .tileset-editor-shell

    if (html != tilesetEditorMarkup_) {
        tilesetEditorMarkup_ = html;
        panel->SetInnerRML(html);
    }
}

void EditorUi::updateSpriteAnimationPlayhead() {
    if (!animationDocument_) return;
    const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
    if (!state.openAssetId) return;
    if (Rml::Element* toggle = animationDocument_->GetElementById("anim-preview-toggle")) {
        toggle->SetClass("active", state.previewPlaying);
    }
    if (spriteAnimationTimelineCount_ == 0) return;
    const std::size_t current =
        std::min(state.previewFrameIndex, spriteAnimationTimelineCount_ - 1);
    for (std::size_t i = 0; i < spriteAnimationTimelineCount_; ++i) {
        if (Rml::Element* chip = animationDocument_->GetElementById(
                "anim-frame-chip-" + std::to_string(i))) {
            chip->SetClass("current", i == current);
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

void EditorUi::setImportHandler(ImportAssetRequest importAsset) {
    importAssetRequest_ = std::move(importAsset);
}

void EditorUi::setImportImageForAnimationHandler(ImportImageRequest importImage) {
    importImageForAnimationRequest_ = std::move(importImage);
}

void EditorUi::setFitViewHandler(WorkspaceRequest fitView) {
    fitViewRequest_ = std::move(fitView);
}

void EditorUi::setAnimationSliceHandler(WorkspaceRequest sliceAnimation) {
    sliceAnimationRequest_ = std::move(sliceAnimation);
}

void EditorUi::setTilesetApplySlicingHandler(WorkspaceRequest applyTilesetSlicing) {
    applyTilesetSlicingRequest_ = std::move(applyTilesetSlicing);
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
    const auto setEntry = [&](const char* id, bool shown) {
        if (Rml::Element* entry = document_->GetElementById(id)) {
            entry->SetClass("hidden", !shown);
            entry->SetAttribute("data-arg", request.targetId);
        }
    };
    setEntry("hctx-set-start",     sceneKind && !alreadyStart);
    setEntry("hctx-del-scene",     sceneKind);
    setEntry("hctx-add-instance",  !sceneKind);
    setEntry("hctx-clone-entity",  !sceneKind);
    setEntry("hctx-del-entity",    !sceneKind);

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
    viewportContextMenuVisible_ = false;
    hierarchyContextMenuVisible_ = false;
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
        || hits("hierarchy-context-menu", hierarchyContextMenuVisible_);
}

void EditorUi::refreshToolbar() {
    if (!document_) return;
    const bool playing = coordinator_.isPlaying();
    // Entering Play freezes authoring: an open context menu must not linger.
    if (playing) hideContextMenus();

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
    setEnabled("btn-play-project", !playing && coordinator_.canPlayProject());
    setEnabled("btn-play-scene",   !playing && coordinator_.canPlayCurrentScene());
    setEnabled("btn-stop",         playing);
    // Undo/Redo are derived affordances: available only with history and outside Play.
    setEnabledBoth("btn-undo", "menu-undo", !playing && coordinator_.canUndo());
    setEnabledBoth("btn-redo", "menu-redo", !playing && coordinator_.canRedo());
    setEnabled("menu-delete-entity",
              !playing && coordinator_.selection().primaryEntity != INVALID_ENTITY);

    const bool hasScene = coordinator_.document().findScene(
        coordinator_.state().activeSceneId) != nullptr;
    // Fit and the Grid/Snap toggles all act on the active scene's view state:
    // with no scene there is nothing to frame or draw a grid over, so a click
    // would be a no-op the coordinator now also rejects (defence in depth,
    // not just a greyed-out button).
    const bool gridActionable = !playing && hasScene;
    setEnabledBoth("btn-fit-view",     "menu-fit-view",     gridActionable);
    setEnabledBoth("btn-grid-visible", "menu-grid-visible", gridActionable);
    setEnabledBoth("btn-grid-snap",    "menu-grid-snap",    gridActionable);
    setEnabled("btn-grid-size", gridActionable);
    // Zoom, unlike Grid/Snap/Fit, tracks the PlaySession's scene while
    // playing (see the zoom-in/out and reset-zoom handlers) — Play always has
    // a real scene, so it stays available then. Only truly nothing-to-zoom
    // (no scene, not playing) disables it.
    const bool canZoom = playing || hasScene;
    setEnabledBoth("btn-zoom-in",  "menu-zoom-in",   canZoom);
    setEnabledBoth("btn-zoom-out", "menu-zoom-out",  canZoom);
    setEnabledBoth("toolbar-zoom", "menu-reset-zoom", canZoom);

    // Central Scene View empty state: shown only when no scene exists to edit.
    if (Rml::Element* empty = document_->GetElementById("viewport-empty")) {
        empty->SetClass("hidden", hasScene || playing);
    }

    const EditorSceneViewState& view = coordinator_.sceneView(currentViewSceneId());
    setActiveBoth("btn-grid-visible", "menu-grid-visible", view.gridVisible && gridActionable);
    setActiveBoth("btn-grid-snap",    "menu-grid-snap",    view.gridSnapEnabled && gridActionable);
    const std::string cellSize = compactNumber(view.gridCellSize);
    if (Rml::Element* el = document_->GetElementById("btn-grid-size"))
        el->SetInnerRML(escapeRml(cellSize) + " <span class=\"icon-caret\">&#xeb5d;</span>");
    if (Rml::Element* el = document_->GetElementById("grid-size-control"))
        el->SetClass("disabled", !gridActionable);
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

void EditorUi::showPointerWorldPosition(const std::optional<Vec2>& worldPosition) {
    if (!document_) return;
    std::string text;
    if (worldPosition) {
        text = "X " + std::to_string(static_cast<long>(std::lround(worldPosition->x)))
             + "  Y " + std::to_string(static_cast<long>(std::lround(worldPosition->y)));
    }
    if (text == pointerReadout_) return;   // per-frame call: write only on change
    pointerReadout_ = text;
    if (Rml::Element* el = document_->GetElementById("toolbar-coords")) {
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
        || action == "add-platformer" || action == "add-tilemap-component") {
        inspector_.closeAddMenu();   // then fall through to execute the add
    }

    if (handleProjectFileAction(action, arg, value)) return;
    if (handleConsoleAction(action, arg, value)) return;
    if (handleAssetsAction(action, arg, value)) return;
    if (handleToolbarAction(action, arg, value)) return;
    if (handleSpriteAnimationAction(action, arg, value)) return;
    if (handleTilesetEditorAction(action, arg, value)) return;
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

bool EditorUi::handleSpriteAnimationAction(const std::string& action, const std::string& arg,
                                           const std::string& value) {
    if (action == "create-sprite-animation") {
        if (!coordinator_.isPlaying() && coordinator_.document().hasImageAsset(arg)) {
            const std::string id = uniqueAnimationAssetId(coordinator_.document(), arg);
            const std::string name = id;
            if (coordinator_.execute(AddSpriteAnimationAssetCommand{id, name}).ok) {
                // The asset is only a container: its first clip carries the sheet.
                if (const SpriteAnimationAssetDef* asset =
                        coordinator_.document().findSpriteAnimationAsset(id)) {
                    coordinator_.execute(AddAnimationClipCommand{
                        id, uniqueClipId(*asset), uniqueClipName(*asset), arg});
                }
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
            // The new clip inherits the sheet currently shown in the editor.
            const std::string clipId = uniqueClipId(*asset);
            if (coordinator_.execute(AddAnimationClipCommand{
                    arg, clipId, uniqueClipName(*asset),
                    editorSheetImageId(*asset,
                        coordinator_.state().spriteAnimationEditor.selectedClipId)}).ok) {
                coordinator_.apply(SelectAnimationClipIntent{arg, clipId});
            }
        }
    } else if (action == "import-animation-sheet") {
        // Import a sprite sheet without leaving the editor: the same importAsset
        // pipeline the Assets panel uses returns the image id, then we start a new
        // animation on it via the same path as create-sprite-animation and open
        // it. A new asset with a first clip carrying the imported sheet.
        if (coordinator_.isPlaying() || !importImageForAnimationRequest_) return true;
        const std::optional<AssetId> imageId = importImageForAnimationRequest_();
        if (!imageId || !coordinator_.document().hasImageAsset(*imageId)) return true;
        const std::string id = uniqueAnimationAssetId(coordinator_.document(), *imageId);
        if (coordinator_.execute(AddSpriteAnimationAssetCommand{id, id}).ok) {
            if (const SpriteAnimationAssetDef* asset =
                    coordinator_.document().findSpriteAnimationAsset(id)) {
                coordinator_.execute(AddAnimationClipCommand{
                    id, uniqueClipId(*asset), uniqueClipName(*asset), *imageId});
            }
            coordinator_.apply(OpenSpriteAnimationEditorIntent{id});
        }
    } else if (action == "commit-animation-clip-name") {
        const std::vector<std::string> parts = splitPipe(arg);
        if (parts.size() != 2) {
            coordinator_.logError("Invalid animation clip reference");
        } else if (value.empty()) {
            coordinator_.logError("Clip name cannot be empty");
        } else {
            coordinator_.execute(RenameAnimationClipCommand{parts[0], parts[1], value});
        }
    } else if (action == "commit-animation-clip-fps") {
        const std::vector<std::string> parts = splitPipe(arg);
        const std::optional<float> parsed = parseNumberField(value);
        if (parts.size() != 2) {
            coordinator_.logError("Invalid animation clip reference");
        } else if (!parsed.has_value()) {
            coordinator_.logError("FPS is not a number");
        } else {
            coordinator_.execute(SetAnimationClipFrameRateCommand{parts[0], parts[1], *parsed});
        }
    } else if (action == "commit-animation-columns"
               || action == "commit-animation-rows"
               || action == "commit-animation-margin"
               || action == "commit-animation-spacing") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Slice grid value is not a number");
        } else {
            const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
            SetAnimationSliceGridIntent intent{
                state.sliceColumns,
                state.sliceRows,
                state.sliceMargin,
                state.sliceSpacing,
            };
            const int rounded = static_cast<int>(std::round(*parsed));
            if (action == "commit-animation-columns") intent.columns = rounded;
            else if (action == "commit-animation-rows") intent.rows = rounded;
            else if (action == "commit-animation-margin") intent.margin = rounded;
            else intent.spacing = rounded;
            coordinator_.apply(intent);
        }
    } else if (action == "slice-animation-grid") {
        // Slice always targets the OPEN animation. Create + select a fresh clip
        // when the open asset has no clip matching the current selection - either
        // no clip yet (fresh Import Sheet) or a selection left over from another
        // animation. This makes the selected clip and the open asset always agree,
        // so a slice can never land on a different animation. Re-slicing a clip
        // that does belong to the open asset is unchanged (fills it in place).
        const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
        if (state.openAssetId) {
            const SpriteAnimationAssetDef* asset =
                coordinator_.document().findSpriteAnimationAsset(*state.openAssetId);
            if (asset) {
                const bool selectionBelongsToOpenAsset =
                    state.selectedClipId
                    && std::any_of(asset->clips.begin(), asset->clips.end(),
                                   [&](const SpriteAnimationClipDef& clip) {
                                       return clip.id == *state.selectedClipId;
                                   });
                if (!selectionBelongsToOpenAsset) {
                    const std::string clipId = uniqueClipId(*asset);
                    if (coordinator_.execute(AddAnimationClipCommand{
                            *state.openAssetId, clipId, uniqueClipName(*asset),
                            editorSheetImageId(*asset, state.selectedClipId)}).ok) {
                        coordinator_.apply(SelectAnimationClipIntent{*state.openAssetId, clipId});
                    }
                }
            }
        }
        if (sliceAnimationRequest_) sliceAnimationRequest_();
    } else if (action == "toggle-animation-preview") {
        coordinator_.apply(SetAnimationPreviewPlayingIntent{
            !coordinator_.state().spriteAnimationEditor.previewPlaying});
    } else if (action == "step-animation-preview") {
        coordinator_.apply(StepAnimationPreviewIntent{arg == "-1" ? -1 : 1});
    } else if (action == "set-animation-preview-frame") {
        const std::optional<float> parsed = parseNumberField(arg);
        if (parsed.has_value() && *parsed >= 0.f) {
            coordinator_.apply(SetAnimationPreviewFrameIntent{
                static_cast<std::size_t>(*parsed)});
        }
    } else if (action == "clear-animation-frames") {
        const std::vector<std::string> parts = splitPipe(arg);
        if (parts.size() == 2) {
            coordinator_.execute(SetAnimationClipFramesCommand{parts[0], parts[1], {}});
        }
    } else if (action == "remove-animation-frame") {
        // Removes the i-th timeline chip, which maps 1:1 to the clip's frames.
        const std::optional<float> parsed = parseNumberField(arg);
        const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
        if (parsed.has_value() && *parsed >= 0.f && state.openAssetId) {
            const SpriteAnimationAssetDef* asset =
                coordinator_.document().findSpriteAnimationAsset(*state.openAssetId);
            const SpriteAnimationClipDef* clip =
                asset ? selectedAnimationClip(*asset, state) : nullptr;
            if (clip) {
                std::vector<SpriteAnimationFrameDef> frames = clip->frames;
                const std::size_t index = static_cast<std::size_t>(*parsed);
                if (index < frames.size()) {
                    frames.erase(frames.begin() + static_cast<std::ptrdiff_t>(index));
                    coordinator_.execute(SetAnimationClipFramesCommand{
                        asset->id, clip->id, std::move(frames)});
                }
            }
        }
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
    } else {
        return false;
    }
    return true;
}

bool EditorUi::handleTilesetEditorAction(const std::string& action, const std::string& arg,
                                         const std::string& value) {
    if (action == "create-tileset-from-image") {
        if (!coordinator_.isPlaying() && coordinator_.document().hasImageAsset(arg)) {
            const std::string id = uniqueTilesetAssetId(coordinator_.document(), arg);
            const TilesetSlicing defaultSlicing;   // 32x32, no margin/spacing
            if (coordinator_.execute(
                    AddTilesetAssetCommand{id, id, arg, defaultSlicing}).ok) {
                coordinator_.apply(OpenTilesetEditorIntent{id});
            }
        }
    } else if (action == "open-tileset-editor") {
        if (!coordinator_.isPlaying()) coordinator_.apply(OpenTilesetEditorIntent{arg});
    } else if (action == "remove-tileset") {
        if (!arg.empty()) coordinator_.execute(RemoveTilesetAssetCommand{arg});
    } else if (action == "close-tileset-editor") {
        // Also serves as Cancel: nothing was ever committed to the document
        // while the editor was open, so discarding the pending state IS the
        // cancel - one action string for both, per the single-entry-point
        // paletto (mirrors close-sprite-animation's own single close path).
        coordinator_.apply(CloseTilesetEditorIntent{});
    } else if (action == "commit-tileset-name") {
        if (!arg.empty() && !value.empty()) {
            coordinator_.execute(RenameTilesetCommand{arg, value});
        }
    } else if (action == "commit-tileset-tile-width" || action == "commit-tileset-tile-height"
               || action == "commit-tileset-margin-x" || action == "commit-tileset-margin-y"
               || action == "commit-tileset-spacing-x" || action == "commit-tileset-spacing-y") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Tileset slicing value is not a number");
        } else {
            TilesetSlicing slicing = coordinator_.state().tilesetEditor.pendingSlicing;
            const int rounded = static_cast<int>(std::round(*parsed));
            if (action == "commit-tileset-tile-width") slicing.tileWidth = rounded;
            else if (action == "commit-tileset-tile-height") slicing.tileHeight = rounded;
            else if (action == "commit-tileset-margin-x") slicing.marginX = rounded;
            else if (action == "commit-tileset-margin-y") slicing.marginY = rounded;
            else if (action == "commit-tileset-spacing-x") slicing.spacingX = rounded;
            else slicing.spacingY = rounded;
            coordinator_.apply(SetPendingTilesetSlicingIntent{slicing});
        }
    } else if (action == "apply-tileset-slicing") {
        if (applyTilesetSlicingRequest_) applyTilesetSlicingRequest_();
    } else {
        return false;
    }
    return true;
}

bool EditorUi::handleHierarchyAction(const std::string& action, const std::string& arg,
                                     const std::string& value, EntityId selected) {
    if (action == "select-entity") {
        coordinator_.apply(SelectEntityIntent{
            static_cast<EntityId>(std::strtoul(arg.c_str(), nullptr, 10))});
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
    (void)value;
    if (action == "import-image") {
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
