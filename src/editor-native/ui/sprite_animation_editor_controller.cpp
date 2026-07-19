#include "editor-native/ui/sprite_animation_editor_controller.h"

#include "editor-native/ui/editor_ui.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/sprite_animation_commands.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

namespace {
const SpriteAnimationClipDef* selectedAnimationClip(
    const SpriteAnimationAssetDef& asset,
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
        const bool exists = std::any_of(
            asset.clips.begin(), asset.clips.end(),
            [&](const SpriteAnimationClipDef& clip) { return clip.id == id; });
        if (!exists) return id;
    }
}

std::string uniqueClipName(const SpriteAnimationAssetDef& asset) {
    int n = 1;
    while (true) {
        const std::string name = "Clip " + std::to_string(n++);
        const bool exists = std::any_of(
            asset.clips.begin(), asset.clips.end(),
            [&](const SpriteAnimationClipDef& clip) { return clip.name == name; });
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

std::string compactNumber(float value) {
    if (!std::isfinite(value)) return {};
    const float rounded = std::round(value);
    if (std::fabs(value - rounded) < 0.0005f) {
        return std::to_string(static_cast<int>(rounded));
    }
    std::string text = std::to_string(value);
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}
} // namespace

SpriteAnimationEditorController::SpriteAnimationEditorController(
    EditorCoordinator& coordinator, Rml::ElementDocument* document)
    : coordinator_(coordinator), document_(document) {}

void SpriteAnimationEditorController::detach() {
    document_ = nullptr;
    importImageRequest_ = {};
    sliceRequest_ = {};
    markup_.clear();
    timelineCount_ = 0;
}

void SpriteAnimationEditorController::setImportImageRequest(ImportImageRequest request) {
    importImageRequest_ = std::move(request);
}

void SpriteAnimationEditorController::setSliceRequest(SliceRequest request) {
    sliceRequest_ = std::move(request);
}

void SpriteAnimationEditorController::refresh() {
    if (!document_) return;
    Rml::Element* panel = document_->GetElementById("animation-editor");
    if (!panel) return;
    const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
    if (!state.openAssetId) {
        document_->Hide();
        timelineCount_ = 0;
        if (!markup_.empty()) {
            markup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }
    const SpriteAnimationAssetDef* asset =
        coordinator_.document().findSpriteAnimationAsset(*state.openAssetId);
    if (!asset) {
        document_->Hide();
        timelineCount_ = 0;
        if (!markup_.empty()) {
            markup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }

    document_->Show();
    document_->PullToFront();
    const SpriteAnimationClipDef* selected = selectedAnimationClip(*asset, state);
    // The clip's frames are the sequence, shown 1:1 in the timeline.
    const std::size_t displayedFrameCount = selected ? selected->frameIds.size() : 0;
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
                "<span class=\"anim-clip-count\">" + std::to_string(clip.frameIds.size())
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
    if (state.pendingResliceConfirm) {
        html += "<div class=\"anim-confirm\">Reslice clears every clip sequence. "
                "<button class=\"panel-btn primary\" data-action=\"confirm-animation-reslice\">Confirm</button>"
                "<button class=\"panel-btn\" data-action=\"cancel-animation-reslice\">Cancel</button></div>";
    }
    if (state.pendingSourceImageId) {
        html += "<div class=\"anim-confirm\">Replace source image with "
              + escapeRml(*state.pendingSourceImageId)
              + " and clear frames/sequences? "
                "<button class=\"panel-btn primary\" data-action=\"confirm-animation-source-image\">Confirm</button>"
                "<button class=\"panel-btn\" data-action=\"cancel-animation-source-image\">Cancel</button></div>";
    }
    html += "<div class=\"anim-tool-group\"><span class=\"anim-tool-label\">Source</span>";
    for (const ImageAssetDef& image : coordinator_.document().data().imageAssets) {
        const bool current = image.assetId == asset->sourceImageAssetId;
        html += "<button class=\"panel-btn";
        if (current) html += " active";
        html += "\" data-action=\"request-animation-source-image\" data-arg=\""
              + escapeRml(image.assetId) + "\">" + escapeRml(image.assetId) + "</button>";
    }
    html += "</div>";
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
            html += "<div id=\"anim-frame-chip-" + index + "\" class=\"anim-frame";
            if (std::find(state.selectedTimelineIndices.begin(),
                          state.selectedTimelineIndices.end(), i)
                != state.selectedTimelineIndices.end()) {
                html += " selected";
            }
            html += "\" data-action=\"select-animation-timeline-index\" data-arg=\"" + index
                  + "\" title=\"Select this timeline occurrence\">"
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
    if (html != markup_) {
        markup_ = html;
        panel->SetInnerRML(html);
    }
    // Playhead highlight + play state live outside the generated markup; they are
    // applied per frame in updateSpriteAnimationPlayhead() via classes only.
    timelineCount_ = displayedFrameCount;
}

void SpriteAnimationEditorController::updatePlayhead() {
    if (!document_) return;
    const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
    if (!state.openAssetId) return;
    if (Rml::Element* toggle = document_->GetElementById("anim-preview-toggle")) {
        toggle->SetClass("active", state.previewPlaying);
    }
    if (timelineCount_ == 0) return;
    const std::size_t current =
        std::min(state.previewFrameIndex, timelineCount_ - 1);
    for (std::size_t i = 0; i < timelineCount_; ++i) {
        if (Rml::Element* chip = document_->GetElementById(
                "anim-frame-chip-" + std::to_string(i))) {
            chip->SetClass("current", i == current);
        }
    }
}

bool SpriteAnimationEditorController::handleAction(const std::string& action, const std::string& arg,
                                           const std::string& value) {
    if (action == "create-sprite-animation") {
        if (!coordinator_.isPlaying() && coordinator_.document().hasImageAsset(arg)) {
            const std::string id = uniqueAnimationAssetId(coordinator_.document(), arg);
            if (coordinator_.execute(CreateSpriteAnimationAssetCommand{
                    id, id, "clip-1", "Clip 1", arg}).ok) {
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
            // The new clip shares the asset sheet; sequences are authored later.
            const std::string clipId = uniqueClipId(*asset);
            if (coordinator_.execute(AddAnimationClipCommand{
                    arg, clipId, uniqueClipName(*asset)}).ok) {
                coordinator_.apply(SelectAnimationClipIntent{arg, clipId});
            }
        }
    } else if (action == "import-animation-sheet") {
        // Import a sprite sheet without leaving the editor: the same importAsset
        // pipeline the Assets panel uses returns the image id, then we start a new
        // animation on it via the same path as create-sprite-animation and open
        // it. A new asset with a first clip carrying the imported sheet.
        if (coordinator_.isPlaying() || !importImageRequest_) return true;
        const std::optional<AssetId> imageId = importImageRequest_();
        if (!imageId || !coordinator_.document().hasImageAsset(*imageId)) return true;
        const std::string id = uniqueAnimationAssetId(coordinator_.document(), *imageId);
        if (coordinator_.execute(CreateSpriteAnimationAssetCommand{
                id, id, "clip-1", "Clip 1", *imageId}).ok) {
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
                            *state.openAssetId, clipId, uniqueClipName(*asset)}).ok) {
                        coordinator_.apply(SelectAnimationClipIntent{*state.openAssetId, clipId});
                    }
                }
                const bool hasAuthoredContent = !asset->frames.empty()
                    || std::any_of(asset->clips.begin(), asset->clips.end(),
                                   [](const SpriteAnimationClipDef& clip) {
                                       return !clip.frameIds.empty();
                                   });
                if (hasAuthoredContent) {
                    coordinator_.apply(RequestAnimationResliceConfirmIntent{});
                    return true;
                }
            }
        }
        if (sliceRequest_) sliceRequest_();
    } else if (action == "confirm-animation-reslice") {
        coordinator_.apply(ConfirmAnimationResliceIntent{});
        if (sliceRequest_) sliceRequest_();
    } else if (action == "cancel-animation-reslice") {
        coordinator_.apply(CancelAnimationResliceIntent{});
    } else if (action == "request-animation-source-image") {
        coordinator_.apply(RequestAnimationSourceImageIntent{arg});
    } else if (action == "confirm-animation-source-image") {
        const SpriteAnimationEditorState& state =
            coordinator_.state().spriteAnimationEditor;
        if (state.openAssetId && state.pendingSourceImageId) {
            const AssetId assetId = *state.openAssetId;
            const AssetId imageId = *state.pendingSourceImageId;
            if (coordinator_.execute(ReplaceAnimationSourceImageCommand{assetId, imageId}).ok) {
                coordinator_.apply(ConfirmAnimationSourceImageIntent{});
            }
        }
    } else if (action == "cancel-animation-source-image") {
        coordinator_.apply(CancelAnimationSourceImageIntent{});
    } else if (action == "select-animation-timeline-index") {
        const std::optional<float> parsed = parseNumberField(arg);
        if (parsed.has_value() && *parsed >= 0.f) {
            const std::size_t index = static_cast<std::size_t>(*parsed);
            std::vector<std::size_t> indices =
                coordinator_.state().spriteAnimationEditor.selectedTimelineIndices;
            const auto it = std::find(indices.begin(), indices.end(), index);
            if (it == indices.end()) indices.push_back(index);
            else indices.erase(it);
            coordinator_.apply(SetAnimationTimelineSelectionIntent{std::move(indices)});
        }
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
            coordinator_.execute(SetAnimationClipFrameIdsCommand{parts[0], parts[1], {}});
        }
    } else if (action == "remove-animation-frame") {
        // Removes the i-th timeline chip, which maps 1:1 to the clip's frameIds.
        const std::optional<float> parsed = parseNumberField(arg);
        const SpriteAnimationEditorState& state = coordinator_.state().spriteAnimationEditor;
        if (parsed.has_value() && *parsed >= 0.f && state.openAssetId) {
            const SpriteAnimationAssetDef* asset =
                coordinator_.document().findSpriteAnimationAsset(*state.openAssetId);
            const SpriteAnimationClipDef* clip =
                asset ? selectedAnimationClip(*asset, state) : nullptr;
            if (clip) {
                std::vector<SpriteFrameId> frameIds = clip->frameIds;
                const std::size_t index = static_cast<std::size_t>(*parsed);
                if (index < frameIds.size()) {
                    frameIds.erase(frameIds.begin() + static_cast<std::ptrdiff_t>(index));
                    coordinator_.execute(SetAnimationClipFrameIdsCommand{
                        asset->id, clip->id, std::move(frameIds)});
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

} // namespace ArtCade::EditorNative
