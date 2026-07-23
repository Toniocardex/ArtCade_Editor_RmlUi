# ADR-0018 — Game View (`viewportSize`) vs World Bounds

**Status:** Accepted  
**Date:** 2026-07-24  
**Scope:** Editor Play zoom, `PlaySceneInfo`, `SetSceneViewportSizeCommand`,
`World` camera init/clamp/follow authority, `cameraStart` IO, removal of host
`cameraStart` overrides  
**Related:** Constitution (single authorities; Edit/Play isolation),
[Architecture](ARTCADE_RMLUI_ARCHITECTURE.md) § Play materialization,
[ADR-0017](ADR-0017-coordinator-document-core-boundaries.md)

## Context

Editor Play rebuilt a camera every frame with:

```text
zoom = Fit(worldSize)
```

Enlarging World Bounds (e.g. 512→1024) therefore shrunk zoom so the entire
level stayed visible. Users perceived “no camera.” Toolbar zoom is
workspace-only (`EditorState::sceneViews`) and must not drive Play.

The model already separates:

| Field | Meaning |
|---|---|
| `SceneDef::worldSize` | Level extent |
| `SceneDef::viewportSize` | Game View (visible world area) |
| `SceneDef::cameraStart` | Top-left of initial view if no Camera Target |
| `CameraTargetComponent` | Follow target → runtime center |

Export already uses `logicalViewport = viewportSize`. Editor Play dropped
`viewportSize` in `PlaySceneInfo` and fitted the world instead.

## Decision

### Authorities

- Persist: `ProjectDocument` only (`viewportSize`, `cameraStart`, world size).
- Runtime camera center: **`World::cameraCenter_` only**. Renderer consumes
  via `setCameraCenter`; it must not feed follow smoothing.
- Play zoom: `Fit(viewportSize)` into the host Scene View rect; pan from
  runtime center relative to world center. Never Fit(worldSize). Never read
  editor pan/zoom during Play.

Do **not** introduce `GameCameraSettings` or a second camera settings model.

### Four blocking gates

1. **`resetCameraForActiveScene()`** on `World::init()` **and**
   `World::onSceneActivated()` (every scene load / Play Scene / Play Project /
   export transition).
2. **Remove** host `resetCameraOnNextFrame_ → setCameraPosition(cameraStart)`
   (and equivalent lifecycle `setCameraPosition(cameraStart)` that races World).
3. Follow/tick always reads **`cameraCenter_`**, never `renderer->getCameraCenter()`.
4. Emit `cameraStart` in editor `sceneToJson`; read it in **legacy** `readScene()`
   as well as the canonical parser.

### Slice A — Game View authoring + Play Fit

- `SetSceneViewportSizeCommand` (finite, `> 0`, whole pixels, no-op, Undo).
- Inspector **Game View** + presets (same Command).
- Soft diagnostic if viewport exceeds world on an axis (not a save veto).
- `PlaySceneInfo` carries `viewportSize` + `cameraStart`.
- Pure `resolvePlayView(...)` for zoom/pan; editor_app calls it.

### Slice C — Init / clamp / shared resolve

- Pure per-axis `clampCameraCenter(world, viewport, center)`.
- Shared `resolveCameraTarget()` (lowest EntityId) for reset snap and tick smooth.
- Reset: target+offset snap, else `cameraStart + viewport/2`, then clamp.
- Tick: smooth from `cameraCenter_`, clamp, push to renderer if present.

### Deferred

- Slice B: Edit-mode Game View rectangle overlay.
- Slice D: authoring zoom, dead zone, look-ahead, shake, multi-cam, Logic camera.

## Consequences

- World width changes no longer change Play zoom when viewport is fixed.
- Play RmlUi, native export, and WASM share camera center policy.
- Scene transitions re-snap camera; no slow pull from world center after load.

## Verification

- `resolvePlayView` zoom equal for world 512 and 1024 with viewport 512.
- Viewport Command Undo/Redo; invalid rejected; Save/Load viewport + cameraStart
  (canonical + legacy).
- No target / auto target reset; follow from World center; clamp edges;
  scene transition; renderer present vs absent same `cameraCenter_`; Stop
  leaves authoring untouched.
