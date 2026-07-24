#pragma once

#include "editor-native/app/inspector_commit.h"

#include <filesystem>
#include <optional>

namespace ArtCade::EditorNative {

// Native "open"/"save as" file pickers. The application layer calls these to let
// the user choose a project path; they return std::nullopt when the dialog is
// cancelled. Platform glue only — no project, coordinator or renderer knowledge.
std::optional<std::filesystem::path> openProjectFileDialog();
std::optional<std::filesystem::path> saveProjectFileDialog(
    const std::filesystem::path& suggested);

// Pickers for importing assets. nullopt when cancelled.
std::optional<std::filesystem::path> openImageFileDialog();
std::optional<std::filesystem::path> openAudioFileDialog();
std::optional<std::filesystem::path> openFontFileDialog();
std::optional<std::filesystem::path> openScriptFileDialog();

// ADR-0019: folder picker for Export destination (Windows IFileDialog).
std::optional<std::filesystem::path> pickExportDestinationFolder(
    const std::filesystem::path& suggested);

// ADR-0020: native RGB color picker (Windows ChooseColor). nullopt when cancelled.
// @p initial is document RGB in [0,1]; returned RGB is also in [0,1].
std::optional<ColorRgb> pickColorRgb(ColorRgb initial);

} // namespace ArtCade::EditorNative
