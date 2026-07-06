#pragma once

#include "core/types.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

class ProjectDocument;

// Generous bound for a 2D editor's chunk edge length; also keeps
// chunkSize*chunkSize (used to check a chunk's cell count) well clear of
// int overflow once chunkSize itself has been validated against this bound.
constexpr int kMaxTilemapChunkSize = 256;

// Validates a TilemapComponent's own fields plus every cross-reference into
// the project (tileset existence, per-cell tile id existence). Returns the
// failure reason, or nullopt if valid. This is the single rule set shared by
// AddTilemapComponentCommand, SetTilemapTilesetCommand (validating a
// hypothetical post-change copy) and ProjectValidator::validate - it must
// not be duplicated at any of those call sites.
std::optional<std::string> validateTilemapComponent(
    const ProjectDocument& document, const TilemapComponent& component);

} // namespace ArtCade::EditorNative
