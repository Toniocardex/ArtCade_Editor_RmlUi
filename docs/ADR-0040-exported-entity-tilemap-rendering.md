# ADR-0040 — Entity-owned Tilemap Rendering in the Exported Runtime

**Status:** Implemented — automated verification and cache performance
characterization complete; visual export revalidation pending

**Date:** 2026-07-28

**Classification:** runtime/export bugfix, runtime materialization, derived
presentation cache, rendering parity

**Scope:** `vendor/artcade-runtime` materialization, runtime ECS,
`GameplaySession`, frame snapshots, entity rendering, runtime tests, the native
export template, and the deterministic visual fixture

**Related:** [ADR-0001](ADR-0001-tilemap-ownership.md),
[ADR-0019](ADR-0019-export-pipeline.md),
`ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` §§4, 9, 11, 16, 22,
`ARTCADE_RMLUI_ARCHITECTURE.md` §§26–28,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §§4, 17, 18, 25, 33, 36

## 1. Decision summary

The exported/standalone runtime will render the entity-owned
`SceneInstanceDef::tilemap` model selected by ADR-0001.

The runtime data flow is:

```text
ProjectDoc
  └─ SceneInstanceDef::tilemap              persistent authority
       ↓ materializeInstance()
     EntityDef::tilemap                     transient mirror only
       ↓ RuntimeEntityGateway
     EntityRegistry::TilemapComponent       session state
       ↓ active-scene cache rebuild
     GameplaySession::resolvedTilemapDraws_ derived presentation cache
       ↓ buildFrameSnapshot()
     RenderableEntitySnapshot::tilemapDraw  immutable observer for one frame
       ↓ scene_entities_pass
     Renderer::drawSpriteRegions()
```

Resolution of tile IDs, chunk cells, atlas source rectangles, and local cell
coordinates happens at scene/session boundaries, not once per frame.
Per-frame work is limited to finding the cached draw record and applying the
entity's current position, layer opacity, and visibility.
The renderer resolves and loads the image texture once for each tilemap draw
sequence; individual cells reuse that prepared, frame-local texture state.

This ADR resolves the standalone/exported-runtime non-decision left open by
ADR-0001. It does not replace or reinterpret the editor-native Edit/Play
implementation documented there.

## 2. Problem

The native editor can author painted `TilemapComponent` data on a placed scene
instance. Scene view and the editor Play preview can render that data, but an
exported executable cannot.

The exported runtime already parses:

```text
SceneInstanceDef::tilemap
```

but the value is lost during materialization:

- `EntityDef` has no tilemap mirror;
- `materializeInstance()` does not copy the instance-owned component;
- `EntityRegistry` and `RuntimeEntityGateway` expose no tilemap read path;
- `GameplaySession::buildFrameSnapshot()` cannot attach tilemap draw data;
- `scene_entities_pass.cpp` only draws sprite, text, and gauge presentation;
- the existing `scene_background_pass.cpp` tilemap renderer consumes only the
  legacy scene-level `SceneDef::tilemap`/`tilemapLayers` representation.

The editor does not author the legacy scene-level representation. Consequently
the exported executable receives valid entity-owned tilemap JSON but never
turns it into draw calls.

This is not an archive path, encryption, asset packing, or current-working-
directory defect. It is a missing additive path in the runtime.

## 3. Goals

This slice must:

1. preserve `SceneInstanceDef::tilemap` as the only persistent authority;
2. carry the component through runtime materialization and ECS storage;
3. resolve static tile presentation once per active-scene lifecycle;
4. render tilemap-only entities even when they have no sprite asset;
5. preserve current editor geometry and layer-order semantics;
6. continue rendering valid cells when shipped content contains an invalid
   cell reference;
7. diagnose invalid data outside the per-frame render loop;
8. leave the legacy scene-level tilemap compatibility path untouched;
9. prove the complete materialization → ECS → snapshot → render input path;
10. refresh and verify the precompiled Windows export template.

## 4. Non-goals

The following are explicitly outside this slice:

- changing the project schema or JSON representation;
- moving tilemap authority to Object Types or `EntityDef`;
- serializing `EntityDef::tilemap`;
- converting entity-owned tilemaps into legacy `TilemapData`;
- removing `SceneDef::tilemap`, `tilemapLayers`, or the legacy background pass;
- runtime tile painting, `Set Tile`, or other dynamic tilemap mutation;
- a public `RuntimeEntityGateway::setTilemap()` API;
- entity transform scale or rotation applied to tile cells;
- support for per-cell `FlipX`, `FlipY`, or `Rot90`;
- tile culling, batching, GPU instancing, or a new render pass;
- a generic diagnostics framework;
- a second entity-discovery loop in the per-frame snapshot build;
- a new CMake target, manager, job system, or worker thread;
- WASM support claims without a separate target verification.

## 5. Authority, mutations, Undo, and persistence

### 5.1 Authority

The authority matrix for this slice is:

| Data | Authority | Lifetime |
|---|---|---|
| Authored tilemap component | `SceneInstanceDef::tilemap` in `ProjectDoc` | persistent |
| Materialized component | `EntityDef::tilemap` | function/session staging |
| Runtime component | `TilemapComponent` in `EntityRegistry` | runtime entity |
| Resolved cell draw data | `GameplaySession::resolvedTilemapDraws_` | active scene/play |
| Per-frame tilemap reference | `RenderableEntitySnapshot::tilemapDraw` | one synchronous frame |

`EntityDef::tilemap`, ECS storage, the resolved cache, and frame snapshot
references are derived and reconstructible. Losing any of them must not lose
authored data.

### 5.2 Intent and Command

No new `EditorIntent` or `EditorCommand` is introduced.

This slice does not mutate authoring data; it fixes consumption of data already
authored through the existing Tilemap commands. Runtime materialization,
cache construction, and rendering are not authoring mutations.

### 5.3 Undo/Redo, dirty state, and revisions

Undo/Redo is not involved. Cache rebuilds and rendering:

- create no Command history entry;
- create no project revision;
- do not alter `savedRevision`;
- do not make the project dirty;
- do not reverse-sync into `ProjectDocument`.

### 5.4 Persistence and migration

No schema bump or migration is required. The current serializer already reads
and writes `SceneInstanceDef::tilemap`.

Entity and Object Type JSON codecs must not read or write the new transient
`EntityDef::tilemap` mirror. A regression test must protect this boundary.

## 6. Core type placement and transient materialization

### 6.1 Complete-type requirement

`TilemapComponent` is currently declared after `EntityDef`. A forward
declaration is insufficient for:

```cpp
std::optional<TilemapComponent>
```

because `std::optional<T>` requires `T` to be complete when instantiated.

The tilemap value types required by `TilemapComponent` must therefore move
before `EntityDef`:

```text
TileId
TileTransformFlags
TilemapCellValue
TilemapCell
TilemapChunk
TilemapComponent
```

`TilesetAsset` does not need to move: `TilemapComponent` stores only the
tileset `AssetId`.

This is a declaration-order change, not a model or serialization change.

### 6.2 `EntityDef` mirror

`EntityDef` gains:

```cpp
// Runtime materialization mirror only.
// Persistent authority remains SceneInstanceDef::tilemap.
// Entity/Object Type JSON codecs must not read or write this field.
std::optional<TilemapComponent> tilemap;
```

The comment is part of the architectural contract.

`materializeInstance()` must assign:

```cpp
e.tilemap = instance.tilemap;
```

unconditionally, after the same authority rule used for `cameraTarget`.
Tilemap is placed-instance authority; it never inherits from the Object Type
and never merges with a type-level component.

Both obsolete comments claiming that `TilemapChunk` or
`TilemapComponent::chunks` are always empty must be updated. Painting is
already shipped and populated chunks are now runtime input.

## 7. Runtime ECS and Gateway contract

### 7.1 Registry

`EntityRegistry` gains internal typed component access matching the existing
Text/Gauge pattern:

```cpp
bool getTilemap(EntityId id, TilemapComponent& out) const;
void setTilemap(
    EntityId id,
    const std::optional<TilemapComponent>& tilemap);
```

Required behavior:

- `getTilemap()` returns `false` for a missing entity or absent component;
- `setTilemap(id, value)` uses `emplace_or_replace`;
- `setTilemap(id, std::nullopt)` removes the component;
- applying a complete `EntityDef` cannot leave a stale prior component.

### 7.2 Public Gateway surface

`RuntimeEntityGateway` exposes only:

```cpp
bool getTilemap(EntityId id, TilemapComponent& out) const;
```

`applyEntityDefToRegistry()` calls the registry directly:

```cpp
registry_->setTilemap(id, def.tilemap);
```

A public gateway setter is forbidden in this slice. It would imply runtime
mutation semantics and cache invalidation obligations that have not been
designed.

### 7.3 Tilemap-only entities

The current materialization path installs `Transform` and `SpriteComponent`
for every runtime entity, even when `sprite.spriteAssetId` is empty.
Therefore the existing stable `forEachActiveRenderable()` discovery remains
the single per-frame discovery path.

A regression test must prove that an active tilemap-only entity:

- is discovered;
- appears in `snapshot.renderables`;
- carries an empty sprite asset without being discarded;
- receives a non-null `tilemapDraw`.

## 8. Runtime-owned resolver

### 8.1 Module boundary

Add:

```text
vendor/artcade-runtime/src/app/render/tilemap_component_resolve.h
vendor/artcade-runtime/src/app/render/tilemap_component_resolve.cpp
```

The resolver is owned by the runtime and must not include or link anything
from `src/editor-native`.

Only the two forward chunk-coordinate formulas may be ported:

```text
cellX = chunkX * chunkSize + localX
cellY = chunkY * chunkSize + localY
```

Reverse mapping, floor division, and authoring paint helpers are not needed.

### 8.2 Resolved representation

The presentation representation is:

```cpp
struct ResolvedTilemapCell {
    int cellX = 0;
    int cellY = 0;
};

struct SpriteRegionDraw {
    float srcX = 0.f;
    float srcY = 0.f;
    float srcW = 0.f;
    float srcH = 0.f;
    float dstX = 0.f; // entity-local
    float dstY = 0.f; // entity-local
    float dstW = 0.f;
    float dstH = 0.f;
};

struct ResolvedTilemapDraw {
    AssetId imageAssetId;
    Vec2 cellSize{};
    std::vector<ResolvedTilemapCell> cells;
    std::vector<SpriteRegionDraw> regions;
};
```

Cell coordinates remain local. World coordinates must never be cached,
because the entity can move during gameplay.
`cells[i]` and `regions[i]` describe the same resolved tile. The cached
destination rectangle is entity-local and is translated by the current entity
position inside `Renderer::drawSpriteRegions()`. Both vectors are allocated
only when the active-scene cache is rebuilt.

### 8.3 Diagnostic result

Resolution must distinguish a component-level failure from recoverable
cell-level omissions. The concrete result is:

```cpp
enum class TilemapResolveIssueCode {
    MissingTileset,
    EmptyTilesetAssetId,
    TilesetAssetMismatch,
    EmptyImageAssetId,
    InvalidChunkSize,
    InvalidCellSize,
    ChunkSizeOverflow,
    MissingTileDefinition,
    InvalidTileDefinition,
    UnsupportedCellTransform,
    InvalidChunkCoordinate,
};

struct TilemapResolveIssue {
    TilemapResolveIssueCode code{};
    TileId tileId;
    int chunkX = 0;
    int chunkY = 0;
};

struct TilemapComponentResolveResult {
    std::optional<ResolvedTilemapDraw> draw;
    std::vector<TilemapResolveIssue> issues;
};
```

Component-level validation failure returns `draw == std::nullopt` plus the
specific component-level issue. A missing tileset is reported by the
cache-building caller as `MissingTileset`, because no `TilesetAsset` exists to
pass to the resolver. Recoverable cell-level failures return a draw containing
all valid cells plus issues.

The exact diagnostic text belongs to the cache-building caller, not the pure
resolver. No logging occurs inside the cell traversal and no diagnostic is
emitted during rendering.

### 8.4 Component-level validation

Before division, modulo, multiplication, or allocation, the resolver must
validate:

- `component.tilesetAssetId` is not empty;
- `tileset.assetId == component.tilesetAssetId`;
- `tileset.imageAssetId` is not empty;
- `component.chunkSize > 0`;
- `component.cellSize.x` and `.y` are finite and greater than zero;
- `chunkSize * chunkSize` is computed with checked arithmetic and is
  representable as `std::size_t`.

Failure of any item invalidates the component draw. It must not crash, divide
by zero, allocate from a wrapped size, or silently produce a valid-looking
empty draw.

### 8.5 Chunk bounds and coordinate safety

For each chunk:

```cpp
expected = checked_square(component.chunkSize);
count = std::min(chunk.cells.size(), expected);
```

Cells beyond `chunkSize²` are ignored. They must not spill logically into a
following row or chunk.

Negative `chunkX` and `chunkY` are valid. Absolute cell coordinates are
computed using a wider checked integer type. A result outside the range of
`int` is skipped with `InvalidChunkCoordinate`; signed overflow is forbidden.

Traversal order is deterministic:

```text
component.chunks vector order
  → cell index 0..count-1
```

The resolver must not sort cells or iterate an unordered container to produce
draw order.

### 8.6 Tile definition validation

A referenced `TileDefinition` is drawable only when:

- the ID resolves in the component's selected tileset;
- `x >= 0` and `y >= 0`;
- `width > 0` and `height > 0`.

Atlas upper bounds cannot be checked by this resolver because
`TilesetAsset` does not own decoded image dimensions. That remains the asset
validation/texture boundary's responsibility.

An unknown ID produces `MissingTileDefinition` and only that cell is skipped.
An invalid definition produces `InvalidTileDefinition` and only cells using
that definition are skipped.

### 8.7 Cell transform policy

Only:

```cpp
TileTransformFlags::None
```

is supported in this slice.

A cell using `FlipX`, `FlipY`, `Rot90`, or any unknown bit is skipped and
reported as `UnsupportedCellTransform`. The runtime must not ignore the flag
and draw a misleading untransformed cell.

Supporting these flags later requires a separate renderer capability and cache
invalidation-compatible slice.

### 8.8 Half-texel atlas inset

To match the editor's atlas bleeding protection, each valid tile source
rectangle is resolved as:

```text
if width > 1 and height > 1:
    srcX = x + 0.5
    srcY = y + 0.5
    srcW = width  - 1.0
    srcH = height - 1.0
else:
    preserve the original rectangle
```

The destination cell size is unchanged. The inset affects only source
sampling.

### 8.9 Empty component

A structurally valid component with zero chunks, or chunks containing only
empty cells, returns:

```text
draw.has_value() == true
draw->cells.empty() == true
```

This distinguishes valid empty authoring from malformed component data.

## 9. Derived presentation cache

### 9.1 Need and owner

Resolving every tile ID and allocating every cell vector inside
`buildFrameSnapshot()` would repeat static work at frame rate. For a large
tilemap this becomes thousands of hash lookups and allocations at 60 FPS.

`GameplaySession` therefore owns:

```cpp
std::unordered_map<EntityId, ResolvedTilemapDraw>
    resolvedTilemapDraws_;
```

This cache is justified by a concrete repeated cost. It is derived,
scene/play-scoped, non-persistent, and reconstructible from the active ECS
plus `SceneManager::tilesets()`.

No second persistent tilemap model is introduced.

### 9.2 Lifecycle API

`GameplaySession` owns private lifecycle helpers:

```cpp
void rebuildActiveSceneTilemapDraws();
void clearResolvedTilemapDraws();
```

They are not general mutation APIs.

### 9.3 Rebuild boundary

The cache is rebuilt only after both conditions hold:

1. the active scene's entity registry/materialization is stable;
2. the current project tileset catalog is installed in `SceneManager`.

Required rebuild boundaries are:

- standalone project/world load;
- editor project load or project replacement;
- editor Enter Play after the runtime world is synchronized;
- editor Exit Play/design-state restore;
- active scene activation;
- scene restart;
- any future path that replaces the runtime world or active scene.

Scene transitions and restarts must use the existing centralized scene
activation lifecycle; they must not add polling to the frame loop.

### 9.4 Clear boundary

The cache is cleared:

- before/while replacing its source world;
- when no active scene exists;
- on failed active-scene preparation;
- on `shutdownGraph()`;
- on session destruction;
- before a rebuild result is installed.

No entry may outlive the `GameplaySession` that owns it.

### 9.5 Rebuild algorithm

The rebuild:

1. constructs a local replacement map;
2. walks active renderables in stable registry insertion order;
3. reads `TilemapComponent` through `RuntimeEntityGateway::getTilemap()`;
4. resolves the matching `TilesetAsset` from
   `SceneManager::tilesets()`;
5. resolves the component;
6. emits deduplicated warnings for issues;
7. inserts valid draws into the replacement map;
8. swaps the replacement into `resolvedTilemapDraws_`.

No persistent second tileset index is required for this slice. A linear
tileset lookup at rebuild time is acceptable and matches the existing legacy
resolver shape.

If a tileset is absent, the entity receives no draw and one warning for that
rebuild. Other tilemap entities still resolve.

Warnings are deduplicated at least by:

```text
entityId + issue code + tileId
```

or, for component-level failures:

```text
entityId + issue code
```

The current runtime diagnostic route may be used; this ADR does not authorize
a generic event bus or logging subsystem. Warnings must never be emitted per
frame.

### 9.6 Runtime entity changes

Runtime entity destruction does not require immediate cache erasure: dead
entities are absent from the frame's active renderable discovery and cannot
attach the stale entry. The map is reclaimed at the next cache boundary.

Runtime creation or mutation of tilemap components is unsupported. When a
future feature introduces it, the same slice must add the public mutation API,
cache invalidation, diagnostics, and tests. A setter must not be added early.

## 10. Frame snapshot contract

`RenderableEntitySnapshot` gains:

```cpp
const ResolvedTilemapDraw* tilemapDraw = nullptr;
```

The field name is deliberately not `tilemap`, to distinguish it from the
legacy scene-level `SceneFrameSnapshot::tilemap` and `tilemapLayers`.

`buildFrameSnapshot()` performs only:

```cpp
const auto it = resolvedTilemapDraws_.find(item.id);
if (it != resolvedTilemapDraws_.end()) {
    entry.tilemapDraw = &it->second;
}
```

It must not:

- copy `TilemapComponent` out of ECS;
- resolve tile IDs;
- scan chunks;
- allocate a cell vector;
- rebuild or mutate the cache.

### 10.1 Pointer lifetime

`tilemapDraw` is a non-owning observer. The frame contract is:

```text
simulation/entity flush
  → build frame snapshot
  → execute render passes synchronously
  → finish/present frame
  → cache may be invalidated
```

`resolvedTilemapDraws_` must not be cleared, rebuilt, erased, or replaced
between snapshot construction and completion of rendering. This matches the
existing synchronous aliasing contract for legacy
`SceneFrameSnapshot::tilemap`/`tilemapLayers`.

The snapshot must not be queued, retained, or rendered on another thread.

## 11. Rendering contract

Entity-owned tilemaps are drawn in `scene_entities_pass.cpp`, not the legacy
background pass and not a new pass.

In the existing first per-renderable draw loop, after position, layer
visibility, and alpha are computed, tile cells are drawn before the entity's
sprite and text:

```cpp
if (item.tilemapDraw) {
    ctx.renderer->drawSpriteRegions(
        item.tilemapDraw->imageAssetId,
        item.tilemapDraw->regions.data(),
        item.tilemapDraw->regions.size(),
        pos.x,
        pos.y,
        alpha);
}
```

`drawSpriteRegions()` resolves and acquires the texture before its loop, then
performs one `DrawTexturePro()` submission per cached region. A missing texture
therefore causes at most one load attempt for that tilemap submission, not one
attempt per cell. A failed tilemap submission must not suppress the same
entity's sprite, text, or gauge rendering.

The existing second gauge loop remains unchanged.

### 11.1 Geometry frozen for v1

The standalone runtime must match current editor behavior:

| Input | Tilemap behavior |
|---|---|
| `Transform.position` | origin of cell `(0, 0)`, evaluated every frame |
| `Transform.scale` | ignored for tile cells |
| `Transform.rotation` | ignored for tile cells |
| `TileTransformFlags::None` | rendered |
| any other cell flag | skipped and diagnosed |
| component `cellSize` | destination width/height |

World-space destinations must not be baked into the cache. A moving entity's
tilemap follows its current runtime position.

### 11.2 Visibility, alpha, layers, and order

The tilemap uses the same entity values already resolved for the sprite path:

- active-scene membership;
- `visibleInGame`/runtime sprite alpha;
- `SpriteComponent::layerId`;
- `SceneLayerSettings::visible`;
- layer opacity;
- layer parallax position adjustment;
- stable order by layer rank, sprite render order, and insertion order.

Drawing is independent of `sprite.spriteAssetId`. An empty sprite asset must
not suppress a tilemap.

No separate tilemap sort or layer system is introduced.

### 11.3 Legacy compatibility

The following remain unchanged:

```text
SceneFrameSnapshot::tilemap
SceneFrameSnapshot::tilemapLayers
scene_background_pass.cpp
TilemapRenderer
```

They remain compatibility-only for legacy scene-level data. There is no
conversion, merge, synchronization, or inference between the legacy and
entity-owned models.

## 12. Failure and validation policy

The two runtime contexts have different responsibilities:

```text
Authoring / Play gate / Export preflight
  → invalid tile references are validation errors

Shipped standalone defensive renderer
  → omit only invalid cells/components
  → warn once at cache rebuild
  → continue rendering valid content
```

The runtime must never substitute:

- another tile;
- a placeholder source rectangle;
- the first tileset;
- the legacy scene tilemap;
- an untransformed cell for an unsupported transform flag.

The export validation path must be verified to reject invalid authored
tilemap references before packaging. If that coverage is absent, adding it is
part of closing this slice; the shipped runtime fallback does not replace
authoring/export validation.

## 13. CMake and module wiring

The resolver implementation must be compiled into the existing shared
`artcade-gameplay-session` target in:

```text
vendor/artcade-runtime/CMakeLists.txt
```

Required source list:

```cmake
add_library(artcade-gameplay-session STATIC
    src/app/src/gameplay_session.cpp
    src/app/src/gameplay_session_seed.cpp
    src/app/render/sprite_frame_resolve.cpp
    src/app/render/text_value_formatter.cpp
    src/app/render/tilemap_component_resolve.cpp
)
```

Adding the file only to `src/app/CMakeLists.txt` is insufficient because the
editor Play facade and tests link `artcade-gameplay-session`.

No new target is authorized.

## 14. Required implementation map

### Runtime core and ECS

- `vendor/artcade-runtime/src/core/types.h`
  - move tilemap value declarations before `EntityDef`;
  - add the transient `EntityDef::tilemap` mirror;
  - update obsolete empty-chunk comments.
- `vendor/artcade-runtime/src/core/object-type-materialize.cpp`
  - copy instance-owned tilemap during materialization.
- `vendor/artcade-runtime/src/modules/runtime-entity-gateway/src/entity-registry.h`
- `vendor/artcade-runtime/src/modules/runtime-entity-gateway/src/entity-registry.cpp`
  - add internal get/set storage.
- `vendor/artcade-runtime/src/modules/runtime-entity-gateway/include/runtime-entity-gateway.h`
- `vendor/artcade-runtime/src/modules/runtime-entity-gateway/src/runtime-entity-gateway.cpp`
  - expose read-only getter;
  - apply the transient mirror to the registry.

### Runtime presentation

- new `vendor/artcade-runtime/src/app/render/tilemap_component_resolve.h`
- new `vendor/artcade-runtime/src/app/render/tilemap_component_resolve.cpp`
- `vendor/artcade-runtime/src/app/src/gameplay_session.h`
- `vendor/artcade-runtime/src/app/src/gameplay_session.cpp`
  - own, rebuild, clear, diagnose, and attach the cache.
- `vendor/artcade-runtime/src/app/render/scene_frame_snapshot.h`
  - add `tilemapDraw` observer and lifetime documentation.
- `vendor/artcade-runtime/src/app/render/passes/scene_entities_pass.cpp`
  - submit the cached cell range with the live entity offset.
- `vendor/artcade-runtime/src/modules/renderer/include/sprite-region-draw.h`
- `vendor/artcade-runtime/src/modules/renderer/include/renderer.h`
- `vendor/artcade-runtime/src/modules/renderer/src/renderer_draw.cpp`
  - acquire the texture once per tilemap and submit the cached regions.
- `vendor/artcade-runtime/CMakeLists.txt`
  - compile the resolver into `artcade-gameplay-session`.

### Editor fixture and export template

- `src/editor-native/app/visual_fixture.cpp`
  - author a deterministic populated tilemap chunk.
- `tests/reference/visual-fixture.artcade`
  - regenerate the committed fixture using `--write-fixture`.
- `src/editor-native/resources/export-templates/windows-x64/`
  - refresh `game.exe`, `runtime-build-info.json`, and generated template
    fingerprint/metadata through the existing refresh script.

Unrelated modified fixture directories or user project assets must not be
overwritten.

## 15. Test specification

### 15.1 Materialization tests

Extend the existing object-type materialization test:

1. a `SceneInstanceDef` with a populated `TilemapComponent` produces an
   `EntityDef` containing exactly that component;
2. a second instance without tilemap produces `nullopt` even if the copied
   prototype was previously populated in test setup;
3. instance tilemap never becomes Object Type authority;
4. Entity/Object Type JSON output does not gain a tilemap field from the
   transient mirror.

### 15.2 Registry/Gateway tests

Test:

- set then get returns the exact component;
- replace updates the component;
- `setTilemap(id, nullopt)` removes it;
- nonexistent entity returns `false`;
- `applyEntityDefToRegistry()` installs or removes the component;
- no public gateway setter is introduced.

### 15.3 Resolver tests

At minimum:

- populated chunk resolves the correct cell and source list;
- negative chunk coordinates resolve correctly;
- non-square `cellSize` is preserved;
- deterministic chunk/cell order;
- unknown `tileId` is skipped and diagnosed;
- invalid tile definition is skipped and diagnosed;
- `chunkSize == 0` returns no draw and performs no division;
- non-finite or non-positive cell size returns no draw;
- tileset ID mismatch returns no draw;
- empty image asset ID returns no draw;
- cells beyond `chunkSize²` do not spill;
- checked coordinate overflow is skipped and diagnosed;
- empty chunks produce a valid empty draw;
- non-`None` flags are skipped and diagnosed;
- half-texel inset matches the editor rule;
- `1x1` source rectangles remain valid and uninset.

### 15.4 Cache and snapshot integration tests

Using `GameplaySession`/the existing characterization fixture:

- active scene cache rebuild resolves tilemap entities once;
- repeated `buildFrameSnapshot()` calls do not rerun the resolver or allocate
  new tile cell vectors;
- a tilemap-only entity appears in renderables;
- `tilemapDraw` has the expected image asset and cell list;
- current runtime transform position remains outside the cache and is used by
  rendering;
- layer/order matches sprite discovery order;
- scene transition/restart rebuilds against the new active scene;
- project/world replacement cannot retain an old cache entry;
- shutdown clears the cache before dependent graph destruction;
- warning deduplication is per rebuild, not per frame.
- the scale characterization builds two entities sharing one tileset, each
  with 16,384 cells, then verifies across 256 snapshots that both cache
  observers and both region buffers retain identity; elapsed time is reported
  diagnostically without a machine-dependent pass/fail threshold.

### 15.5 Render-path regression

Where the renderer test double can capture commands, assert that:

- each tilemap produces one `drawSpriteRegions` resource acquisition;
- each valid cell still produces one `DrawTexturePro` submission;
- destination equals current entity position plus local cell offset;
- layer opacity is included in alpha;
- a tilemap-only entity draws;
- tile cells are emitted before the same entity's sprite/text;
- unsupported cells emit no draw command.

If the current renderer cannot capture calls without a disproportionate
refactor, the snapshot integration test plus the mandatory exported
end-to-end test is the accepted coverage for this exclusively visual edge.
That exception must be recorded in the implementation PR.

### 15.6 Deterministic fixture

The canonical builder must add at least one deterministic populated chunk to
the Ground entity, for example a row using stable tile IDs from the generated
tileset.

The authoritative committed UI fixture path is:

```text
tests/reference/visual-fixture.artcade
```

The fixture must be regenerated by the builder, not hand-edited. Running
`--write-fixture` again must preserve the painted Ground rather than empty it.

### 15.7 End-to-end export regression

The final acceptance sequence is:

1. build and run runtime tests with
   `vendor\artcade-runtime\build_native.bat`;
2. build and run editor tests with `scripts\build.bat --test`;
3. run `scripts\refresh-export-templates.bat`;
4. verify every refreshed template artifact and build-info/fingerprint file;
5. open the canonical fixture or the reproducing user project;
6. Export → build;
7. run the produced executable from a directory independent of the editor;
8. visually confirm Ground cells render with Player/Coin and match Scene
   view/Play placement and atlas sampling;
9. confirm a project with one deliberately invalid tile reference logs one
   warning and still draws its valid cells.

The template refresh is mandatory: exported builds package a precompiled
`game.exe`; rebuilding runtime sources alone cannot change an exported game.

## 16. Performance acceptance

The implementation is acceptable only if:

- tile ID/chunk resolution occurs at documented cache boundaries;
- `buildFrameSnapshot()` performs at most one cache lookup per discovered
  renderable;
- render performs no tile ID lookup and no cell-vector allocation;
- the image texture is resolved and loaded once per tilemap draw sequence,
  never once per cell;
- warnings are not emitted per frame;
- source coordinates are stored once, while destination coordinates continue
  to use the live entity position;
- no project-wide scan or serialization is added to the frame loop.

A benchmark is not required to justify avoiding an evident
cells × entities × frames repeated allocation. If future profiling shows cell
draw submission itself is too expensive, culling/batching is a separate slice.

`drawSpriteRegions()` is not GPU batching: it performs one texture acquisition
and then one `DrawTexturePro()` submission per cell. The certain CPU cost of
submission therefore remains linear in the number of rendered cells.

## 17. Alternatives considered

### A. Resolve in `buildFrameSnapshot()` every frame

Rejected. It is functionally simple but repeats tileset lookup, tile-ID
mapping, chunk traversal, vector allocation, and component copying at frame
rate for static data.

### B. Convert entity tilemaps to legacy `TilemapData`

Rejected. The legacy representation is scene/layer-owned, loses the direct
entity ownership model, and would create conversion and synchronization
questions around layer, position, cell size, and multiple tilemap entities.

### C. Teach the legacy background pass to read entity components

Rejected. It would need a second entity discovery and ordering path and would
separate tilemaps from their owning entity's render order.

### D. Add tilemap drawing to the entity pass with no cache

Rejected as the final design. It fixes visibility but leaves an avoidable
static resolution cost in every frame.

### E. Store world-space destinations in the cache

Rejected. Runtime movement can change `Transform.position`; cached world
coordinates would become stale or require continuous synchronization.

### F. Add a public runtime tilemap setter now

Rejected. No consumer exists, and its semantics would be incomplete without
dynamic cache invalidation and authored/runtime mutation policy.

### G. Implement cell flip/rotation in this slice

Rejected. The current `drawSpriteRegions()` path has no per-cell transform
contract and the current authoring path emits only `None`.

## 18. Consequences

### Positive

- exported games render the same entity-owned Ground data as the editor;
- persistent authority remains unchanged;
- static cell resolution no longer becomes a frame-rate cost;
- tilemaps participate in entity layer/order/visibility behavior;
- malformed individual cells do not blank the rest of a shipped tilemap;
- diagnostics remain outside the render loop;
- future dynamic tilemap work has an explicit invalidation boundary to extend.

### Costs

- `GameplaySession` owns one additional scene-scoped derived cache;
- frame snapshots contain a non-owning pointer whose lifetime must remain
  synchronous and documented;
- scene/project lifecycle paths gain explicit cache rebuild/clear calls;
- tests must cover more than the isolated resolver;
- binary export templates must be refreshed and reviewed.

### Risks and mitigations

| Risk | Mitigation |
|---|---|
| Cache becomes a second authority | no serialization, rebuild from ECS/tilesets, clear at lifecycle boundaries |
| Stale pointer in snapshot | synchronous frame contract; no cache mutation during rendering |
| Stale scene data after transition | rebuild in centralized scene activation/restart boundary |
| Runtime crash on malformed chunks | validate sizes, checked multiplication/coordinates, bounded traversal |
| Silent missing cells | structured issues and deduplicated rebuild-time warnings |
| Export/editor atlas seams differ | shared half-texel rule |
| Tile flags render incorrectly | accept only `None`; skip and diagnose others |
| Runtime source changes do not reach exports | mandatory template refresh and real exported executable test |

## 19. Rollback

Rollback is source- and template-local:

1. revert runtime materialization/ECS/cache/snapshot/render changes;
2. rebuild the last known-good runtime;
3. refresh the export template from that build;
4. revert fixture changes if they only existed for this regression.

No project migration or user-data rollback is required because this ADR does
not change persisted data.

## 20. Definition of Done

The slice is complete only when:

- [ ] `SceneInstanceDef::tilemap` remains the single persistent authority;
- [ ] `EntityDef::tilemap` is documented and tested as transient only;
- [ ] no Entity/Object Type codec serializes the transient mirror;
- [ ] ECS get/set/remove behavior is tested;
- [ ] the public gateway exposes only the getter;
- [ ] resolver validation and defensive cell policy are tested;
- [ ] only `TileTransformFlags::None` is rendered;
- [ ] the half-texel rule matches the editor;
- [ ] cache ownership, rebuild, clear, and pointer lifetime are implemented;
- [ ] no tilemap resolution/allocation occurs per frame;
- [ ] tilemap-only entities reach the snapshot and draw path;
- [ ] scene transition, restart, replace, and shutdown tests are green;
- [ ] materialization, gateway, resolver, snapshot, and regression tests pass;
- [ ] the deterministic Ground fixture contains painted cells;
- [ ] runtime and editor builds/tests pass;
- [ ] the Windows export template and metadata are refreshed;
- [ ] a real exported executable visibly renders the Ground tile grid;
- [ ] malformed shipped cell references warn once and do not suppress valid
      cells;
- [ ] the full diff is reviewed in both the editor and runtime source
      topology actually used by the workspace.
