#pragma once

#include "editor-native/model/project_document.h"

namespace ArtCade::EditorNative {

// A Tile Palette is useful only for a Tilemap whose source can actually be
// painted: the referenced tileset and image must still resolve and slicing
// must have produced at least one tile. This is a derived UI capability, never
// persistent authoring state.
inline bool tilemapHasPaintableTileset(const ProjectDocument& document,
                                       const SceneInstanceDef& instance) {
    if (!instance.tilemap.has_value()) return false;
    const TilesetAsset* tileset =
        document.findTilesetAsset(instance.tilemap->tilesetAssetId);
    return tileset != nullptr
        && document.findImageAsset(tileset->imageAssetId) != nullptr
        && !tileset->tiles.empty();
}

} // namespace ArtCade::EditorNative
