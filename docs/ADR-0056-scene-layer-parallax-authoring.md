# ADR-0056 — Scene Layer Parallax Authoring and Runtime Parity

**Status:** Accepted  
**Date:** 2026-08-02  
**Related:** ADR-0001, ADR-0018, ADR-0020, ADR-0034  
**Scope:** Native editor authoring for per-scene layer parallax, persistence parity, command ownership, Layer Manager UX, and verification of the existing runtime render path.  
**Out of scope:** Layer background-image authoring, layer opacity authoring, persistent layer visibility controls, auto-scroll authoring, legacy scene-level tilemap parallax, Edit-mode parallax preview, physics displacement, collision displacement, multiple cameras, shaders, and a general rendering rewrite.

---

## 1. Context

ArtCade already contains a runtime representation and a runtime implementation for per-layer parallax:

```cpp
struct LayerParallax {
    float x = 1.f;
    float y = 1.f;
};

struct SceneLayerSettings {
    bool            visible = true;
    float           opacity = 1.f;
    LayerParallax   parallax;
    LayerBackground background;
};

struct SceneDef {
    // ...
    std::vector<SceneLayerDef> layers;
    std::string defaultLayerId;
    std::unordered_map<std::string, SceneLayerSettings> layerSettings;
};
```

The runtime render pipeline already consumes `SceneDef::layerSettings`:

- layer backgrounds are rendered through the existing `ParallaxRenderer`;
- entity-owned sprites, tilemaps, world-space text, and world-space gauges are shifted by the layer parallax factor;
- `SceneDef::layers` remains the single back-to-front render-order authority;
- entity transforms, collision geometry, physics bodies, camera targets, Logic Board state, and simulation order remain unchanged.

The native editor does not currently expose `SceneLayerSettings::parallax` in the Layer Manager. More importantly, the native editor serializer writes the layer stack but does not write `SceneDef::layerSettings`. A project loaded through the canonical reader may therefore contain valid layer settings that are silently lost on the next editor save.

This ADR closes the authoring and persistence gap. It does not introduce a second parallax model or a new renderer.

---

## 2. Decision Summary

Parallax is authored per scene layer through the existing:

```cpp
SceneDef::layerSettings[layerId].parallax
```

The native editor will expose the active layer's X and Y factors inside the existing **Layer Manager** section of the Scene Inspector.

The implementation must follow the normal authoring flow:

```text
Layer Manager field
→ UI action
→ Editor Intent / Command
→ ProjectDocument
→ SceneDef.layerSettings[layerId]
→ dirty revision / Undo / Redo
→ canonical JSON
→ GameplaySession / exported runtime
→ existing render passes
```

The following are explicitly prohibited:

```text
ParallaxManager
Editor-only parallax model
Per-instance parallax overrides
A second scene-layer settings map
Direct Inspector mutation of ProjectDocument
PlaySession-side synchronization from UI state
Per-frame copying from editor controls into runtime state
```

---

## 3. Architectural Invariants

### 3.1 ProjectDocument remains the sole authoring authority

`ProjectDocument` owns the canonical `ProjectDoc`. The Inspector reads through const queries and mutates only through commands.

Parallax authoring must not be stored in:

- `EditorState`;
- `EditorUiState`;
- `EditorSceneViewState`;
- `InspectorPanel`;
- `PlaySession`;
- a renderer-owned cache.

Those systems may hold transient UI state, but the authored factors live only in `SceneDef::layerSettings`.

### 3.2 SceneDef.layers remains the sole layer identity and order authority

`SceneDef::layers` continues to define:

- stable layer identity;
- display name;
- editor lock;
- back-to-front order.

`SceneDef::layerSettings` is keyed by the stable `SceneLayerDef::id`. It does not define order and must never contain a second layer list.

### 3.3 Parallax is presentation-only

Parallax changes only the rendered position.

It must not change:

- `SceneInstanceDef::transform`;
- runtime `Transform`;
- physics positions or velocities;
- collision AABBs;
- ground support;
- camera target resolution;
- Logic Board coordinates;
- tilemap cell coordinates;
- entity containment diagnostics;
- save data other than the layer setting itself.

### 3.4 Edit and Play remain distinct

Normal Edit mode displays authored world coordinates without parallax.

Play and exported builds display parallax through the runtime render pipeline.

This preserves predictable authoring for:

- grid and snap;
- entity selection;
- transform gizmos;
- tilemap painting;
- collider overlays;
- world bounds;
- coordinate readouts.

An Edit-mode parallax preview requires projected picking and transform inversion and is not part of this slice.

---

## 4. Existing Data Model

No new persistent type is required.

The canonical model already provides:

```cpp
struct LayerParallax {
    float x = 1.f;
    float y = 1.f;
};

struct SceneLayerSettings {
    bool            visible = true;
    float           opacity = 1.f;
    LayerParallax   parallax;
    LayerBackground background;
};
```

Default semantics:

```text
parallax.x = 1.0
parallax.y = 1.0
```

A missing `scene.layerSettings[layerId]` entry is equivalent to a default-constructed `SceneLayerSettings`.

The map remains sparse. A scene with ten normal layers does not need ten redundant default entries.

### 4.1 Do not move parallax into SceneLayerDef

The following change is rejected:

```cpp
struct SceneLayerDef {
    std::string id;
    std::string name;
    bool locked;
    float parallaxX;
    float parallaxY;
};
```

Identity/order metadata and visual settings already have separate responsibilities. Moving or duplicating the fields would create two possible authorities and break canonical runtime parity.

### 4.2 Do not add per-instance overrides

The following is also rejected:

```cpp
SceneInstanceDef::parallaxOverride
```

All content assigned to a layer shares that layer's presentation transform. Per-instance parallax would undermine the meaning of a render layer and complicate sorting, picking, and authoring.

---

## 5. Parallax Semantics

ArtCade uses camera top-left coordinates in the render path.

For authored world position `W`, camera top-left `C`, and layer factor `F`, the projected world position submitted to the normal world camera is:

```cpp
projectedWorld = W + C * (1 - F);
```

After the normal camera transform, the screen position is equivalent to:

```cpp
screen = W - C * F;
```

The operation is component-wise:

```cpp
projectedWorld.x = world.x + cameraTopLeft.x * (1.f - parallax.x);
projectedWorld.y = world.y + cameraTopLeft.y * (1.f - parallax.y);
```

Factor meaning:

| Factor | Meaning |
|---:|---|
| `1.0` | Normal world movement |
| `0.0` | Fixed relative to the camera on that axis |
| `0.0 < f < 1.0` | Slower movement; visually farther away |
| `f > 1.0` | Faster movement; visually nearer/foreground |
| `f < 0.0` | Reverse movement; valid advanced effect |

The authoring validator accepts any finite value. It does not impose an arbitrary range that the existing runtime model does not require.

Non-finite values are always rejected:

```text
NaN
+Infinity
-Infinity
```

---

## 6. ProjectDocument Queries and Mutation

Add a read-only query that resolves sparse defaults:

```cpp
SceneLayerSettings ProjectDocument::effectiveLayerSettings(
    const SceneId& sceneId,
    const std::string& layerId) const;
```

Contract:

```text
missing scene
→ default SceneLayerSettings

missing layer
→ default SceneLayerSettings

existing layer without map entry
→ default SceneLayerSettings

existing layer with map entry
→ stored settings
```

The query returns by value. Callers must not retain pointers into the unordered map.

Add one private authoring mutator:

```cpp
bool ProjectDocument::setSceneLayerParallax(
    const SceneId& sceneId,
    const std::string& layerId,
    LayerParallax parallax);
```

The mutator must:

1. reject a missing scene;
2. reject a missing layer;
3. reject non-finite X or Y;
4. obtain the current effective `SceneLayerSettings`;
5. replace only `settings.parallax`;
6. preserve `visible`, `opacity`, and `background`;
7. erase the sparse map entry when the complete settings object returns to its canonical defaults;
8. otherwise store the complete settings object;
9. allocate exactly one new document revision.

### 6.1 Canonical default test

Add a private pure helper:

```cpp
bool isDefaultSceneLayerSettings(const SceneLayerSettings& value);
```

The complete default is:

```cpp
value.visible == true
value.opacity == 1.f
value.parallax.x == 1.f
value.parallax.y == 1.f
value.background.imageId.empty()
value.background.tileX == true
value.background.tileY == true
value.background.scrollX == 0.f
value.background.scrollY == 0.f
```

Use field-wise equality for persistence semantics. A deliberately authored value such as `0.999999f` is not the default and must not be erased by an epsilon comparison.

---

## 7. Command

Add:

```cpp
class SetSceneLayerParallaxCommand final : public EditorCommand {
public:
    SetSceneLayerParallaxCommand(
        SceneId sceneId,
        std::string layerId,
        LayerParallax parallax);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;

    const char* name() const override {
        return "SetSceneLayerParallax";
    }

private:
    SceneId       sceneId_;
    std::string   layerId_;
    LayerParallax next_{};
    LayerParallax previous_{};
    bool          captured_ = false;
};
```

### 7.1 Apply contract

`apply()` must:

- resolve the target scene and layer;
- reject non-finite values;
- read the current effective factors;
- return a true no-op when both factors are unchanged;
- capture the previous pair only on first apply;
- call `ProjectDocument::setSceneLayerParallax`;
- return `DomainChange::sceneChanged(sceneId_)`;
- invalidate at least `Inspector | Viewport`.

### 7.2 Undo/redo contract

Undo restores the exact previous X/Y pair through the same document mutator.

Redo reuses the previously accepted command. It must not depend on current Inspector state.

### 7.3 Atomic pair

The command owns both axes even when the user edits one field.

Example:

```text
current = {0.35, 0.60}
user commits X = 0.50
command next = {0.50, 0.60}
```

This avoids partially updated authoring state and provides deterministic Undo/Redo.

### 7.4 Layer lock

A locked layer protects instance-owned authoring content. It does not freeze the layer definition itself.

Parallax remains editable on a locked layer, just as the layer can still be:

- selected;
- renamed;
- reordered;
- unlocked.

Play mode still disables the control because the entire authoring document is immutable during Play.

---

## 8. Layer Lifecycle

Layer settings are keyed by stable layer ID and must follow layer lifecycle atomically.

### 8.1 Add layer

Adding a layer creates no `layerSettings` entry.

The effective value is automatically:

```cpp
LayerParallax{1.f, 1.f}
```

### 8.2 Rename layer

Renaming changes only `SceneLayerDef::name`.

The stable ID does not change, therefore the settings map remains untouched.

### 8.3 Reorder layer

Reordering changes only `SceneDef::layers`.

The settings map remains keyed by layer ID and is not reordered or copied.

### 8.4 Remove layer

Removing a layer must erase:

```cpp
scene.layerSettings[layerId]
```

in the same `ProjectDocument` mutation and the same dirty revision.

`RemoveSceneLayerCommand` must capture:

```cpp
std::optional<SceneLayerSettings> removedSettings_;
```

Undo must restore the layer at its original index and restore its optional settings entry exactly.

Do not implement removal as two independent document mutations, because that would allocate multiple revisions for one command.

A suitable private primitive is:

```cpp
bool ProjectDocument::addSceneLayer(
    const SceneId& sceneId,
    const std::string& layerId,
    const std::string& name,
    std::size_t index,
    std::optional<SceneLayerSettings> settings = std::nullopt);
```

or an equivalent atomic restore helper.

The default layer remains non-removable.

---

## 9. Persistence

### 9.1 Current gap

The canonical reader already accepts:

```json
"layerSettings": {
  "<layer-id>": {
    "visible": true,
    "opacity": 1.0,
    "parallax": {
      "x": 1.0,
      "y": 1.0
    },
    "background": {
      "imageId": "",
      "tileX": true,
      "tileY": true,
      "scrollX": 0.0,
      "scrollY": 0.0
    }
  }
}
```

The native editor writer currently omits `SceneDef::layerSettings`. This must be fixed before exposing parallax authoring.

### 9.2 Writer contract

`sceneToJson()` must serialize `scene.layerSettings`.

The map may be omitted when empty.

For every emitted map entry, serialize the complete `SceneLayerSettings`, not only the parallax pair. This preserves existing or externally authored:

- persistent visibility;
- opacity;
- background image;
- tiling flags;
- auto-scroll values.

Example:

```json
{
  "id": "scene-1",
  "layers": [
    {
      "id": "background",
      "name": "Background",
      "locked": false
    }
  ],
  "defaultLayerId": "background",
  "layerSettings": {
    "background": {
      "visible": true,
      "opacity": 1.0,
      "parallax": {
        "x": 0.35,
        "y": 0.60
      },
      "background": {
        "imageId": "",
        "tileX": true,
        "tileY": true,
        "scrollX": 0.0,
        "scrollY": 0.0
      }
    }
  }
}
```

### 9.3 Validation

Project validation must reject:

- a `layerSettings` key that does not reference a real layer in the same scene;
- non-finite opacity;
- opacity outside `[0, 1]`;
- non-finite parallax factors;
- non-finite background scroll values;
- a non-empty background image ID that cannot resolve, when background-image data is present.

The canonical reader may retain defensive defaults, but a native editor save must never create malformed authoring data.

### 9.4 Versioning

No project schema bump is required.

Reason:

- `SceneLayerSettings` already exists in the current canonical model;
- the canonical reader already parses `layerSettings`;
- runtime frame snapshots already carry the map;
- runtime render passes already consume the factors.

This ADR restores writer parity and exposes existing data. It does not introduce a new file-format concept.

No Logic Board API or feature flag changes are required.

---

## 10. Scene Inspector / Layer Manager UX

Parallax controls belong inside the existing **Layers** section of the Scene Inspector.

They do not belong in:

- Scene Appearance;
- World Bounds;
- Game View;
- an entity Inspector;
- a project-level settings page.

The property belongs to one `SceneLayerDef` identity within one `SceneDef`.

### 10.1 Selection authority

Use the existing:

```cpp
coordinator.activeLayerId(sceneId)
```

Do not add:

```text
selectedLayerId
LayerInspectorSelection
ParallaxSelectionManager
```

The active layer is already the authoring scope used by the Layer Manager.

### 10.2 Layout

Render the layer rows as they are today, followed by the active layer settings.
The visual result is the primary choice; numeric factors are a secondary fine-tune path:

```text
LAYER MANAGER

[eye] [lock] Hero
[eye] [lock] Background

PARALLAX                         Background layer
Choose how this layer moves with the camera.

Camera movement
[ Fixed ] [ Far ] [ Normal ] [ Near ]

FINE TUNE
Horizontal   [ 0.350 ]
Vertical     [ 0.600 ]

Preview the effect in Play mode.

[ + Add Layer ]
```

The exact placement of **Add Layer** may follow the existing panel spacing, but the settings must visually belong to the active layer rather than to the scene.

### 10.3 Fields

Add the following actions:

```text
commit-layer-parallax-x
commit-layer-parallax-y
apply-layer-parallax-preset
reset-layer-parallax
```

Field behavior:

- parse using the existing authoring-number parser;
- require a finite float;
- display using `formatAuthoringFloat`;
- commit on the editor's established text-field commit event;
- report invalid input through the existing Inspector validation/diagnostic path;
- never silently ignore malformed input;
- issue one command per accepted commit;
- never mutate on every keypress.

The semantic presets issue the same atomic pair command used by the fields:

| Preset | X | Y | Meaning |
|---|---:|---:|---|
| Fixed | `0.0` | `0.0` | Fixed to the game view |
| Far | `0.5` | `0.5` | Slower, distant background movement |
| Normal | `1.0` | `1.0` | Normal world movement |
| Near | `1.5` | `1.5` | Faster, foreground movement |

`Normal` retains the existing reset action and issues:

```cpp
SetSceneLayerParallaxCommand{
    sceneId,
    activeLayerId,
    LayerParallax{1.f, 1.f}
};
```

Presets are conveniences, not a restricted domain range. Any finite factor remains
authorable through **Fine tune**, including asymmetric, negative, and values above
the provided presets. A preset is selected only when both stored axes exactly match it.

### 10.4 Help text

Preset tooltips describe their visual result without requiring factor terminology.
Fine tune retains the advanced explanation:

> Use different horizontal and vertical movement for advanced effects.

Add a small note:

> Preview the effect in Play mode.

### 10.5 Disabled states

Disable parallax editing when:

- no scene is active;
- no valid active layer resolves;
- Play is running.

Do not disable it merely because the layer is locked.

### 10.6 Workspace eye icon remains independent

The existing Layer Manager eye icon controls:

```cpp
EditorSceneViewState::hiddenLayerIds
```

It is an editor-only declutter control.

It must not mutate:

```cpp
SceneLayerSettings::visible
```

This ADR must not merge workspace visibility and persistent runtime visibility.

---

## 11. Runtime Rendering

### 11.1 Reuse the existing render path

The existing runtime already applies layer settings through:

- `SceneFrameSnapshot::layerSettings`;
- `ParallaxRenderer` for repeating layer backgrounds;
- the scene-entities pass for entity-owned visuals.

No new renderer, render graph, or Play-specific model is required.

### 11.2 Entity-owned visual coverage

For non-screen-space content, the layer factor applies to the owning entity's rendered origin:

- sprite;
- animated sprite;
- entity-owned tilemap;
- world-space text;
- world-space gauge.

All visuals owned by one entity must use the same projected origin for the frame.

### 11.3 Screen-space content

Screen-space text and gauges are HUD content. They must ignore layer parallax even if their owning entity belongs to a parallax layer.

Required correction in the existing entity render pass:

```cpp
const Vec2 worldDrawPos =
    layerDrawPos(sprite.layerId, transform.position);

const Vec2 textBase =
    text.screenSpace ? transform.position : worldDrawPos;

const Vec2 gaugeBase =
    gauge.screenSpace ? transform.position : worldDrawPos;
```

Without this correction, a screen-space visual can receive a camera-dependent parallax offset before the renderer interprets it as screen coordinates.

### 11.4 Render order

Parallax does not affect ordering.

The order remains:

```text
SceneDef.layers index 0
→ ...
→ SceneDef.layers last
```

Within the runtime's existing per-layer ordering, current render-order rules remain unchanged.

### 11.5 Simulation independence

The following values remain authored/runtime world truth:

```text
Transform.position
Transform.velocity
BoxCollider2D bounds
CollisionWorld shapes
Platformer ground support
CameraTarget position
Logic Board movement queries
Tilemap cell coordinates
```

Only the draw position is projected.

---

## 12. Edit View Policy

The normal native Scene View will not apply parallax in this slice.

Reasons:

1. the Scene View is an authoring coordinate surface;
2. grid and snap operate in authored world coordinates;
3. collider overlays must align with authored transforms;
4. transform gizmos must commit canonical positions;
5. tilemap painting must remain cell-accurate;
6. picking projected layers requires a layer-aware inverse transform;
7. an always-on preview would make a presentation effect look like a document mutation.

The Layer Manager exposes the factors while Play provides the authoritative preview.

A future workspace-only preview may be added under a separate decision. It must:

- be disabled by default;
- produce no dirty revision;
- use the same formula as runtime;
- project rendering, picking, selection bounds, and gizmos consistently;
- preserve authored transforms;
- define layer-aware drag inversion.

A partial render-only preview is prohibited because it would make visuals and hit targets diverge.

---

## 13. Play and Export Parity

Editor Play already materializes through the canonical project serialization and runtime loader.

Once `sceneToJson()` writes `layerSettings`, Play must receive the same values as a standalone build without editor-side synchronization.

Required parity contract:

```text
ProjectDocument
→ serialize project.json
→ canonical AssetLoader
→ SceneDef.layerSettings
→ GameplaySession frame snapshot
→ existing render passes
```

No direct call from the Inspector to `PlaySession` is allowed.

If vendored runtime code changes for the screen-space correction, rebuild and refresh the Windows runtime template according to the existing runtime build-identity process before publishing the commit.

---

## 14. Tests

### 14.1 ProjectDocument

Add tests for:

- a layer without settings resolves to `{1, 1}`;
- setting parallax creates one sparse settings entry;
- changing X preserves Y and every other settings field;
- changing Y preserves X and every other settings field;
- non-finite values are rejected;
- unchanged values are a command no-op;
- reset to `{1, 1}` erases a fully default entry;
- reset preserves an entry with non-default opacity/background;
- editing a locked layer succeeds;
- a missing scene or layer fails without dirtying the document;
- one accepted command allocates exactly one revision.

### 14.2 Undo/Redo

Verify:

- undo restores the exact previous pair;
- redo restores the exact next pair;
- revision-based dirty state is correct;
- undo after reset restores the sparse entry;
- redo after undo is independent of current Layer Manager selection.

### 14.3 Layer lifecycle

Verify:

- new layers have effective `{1, 1}` and no map entry;
- rename preserves settings by stable ID;
- reorder preserves settings by stable ID;
- remove erases the keyed settings atomically;
- undo remove restores the exact settings and original index;
- default-layer removal remains rejected;
- no orphan map entry survives removal.

### 14.4 Persistence

Add round-trip tests for:

- X/Y factors;
- all `SceneLayerSettings` fields together;
- empty sparse map omission;
- missing entry defaults;
- multiple layers with different factors;
- an orphan key rejected by validation;
- NaN/infinity rejected;
- save/reopen preserves externally loaded opacity/background settings;
- project schema version remains unchanged.

### 14.5 Runtime rendering

Use pure or headless render-math tests where possible:

```text
factor {1,1}
→ projected position equals authored position

factor {0,0}
→ screen position remains camera-fixed

factor {0.5,1}
→ only X moves at half camera speed

factor {1,0.25}
→ only Y uses parallax

factor > 1
→ foreground movement is faster
```

Also verify:

- sprite and animated sprite use the same projection;
- entity-owned tilemap uses the owning layer projection;
- world-space text and gauge use the projection;
- screen-space text and gauge ignore it;
- layer opacity still multiplies alpha;
- runtime layer visibility still gates drawing;
- render order is unchanged;
- transforms and collision geometry remain unchanged.

### 14.6 Editor UI

Verify:

- selecting a layer updates the displayed factors;
- the default layer is editable;
- a locked layer is editable;
- fields are disabled during Play;
- invalid input reports an error and creates no command;
- reset emits one command;
- the workspace eye icon remains independent;
- normal Edit rendering remains unshifted;
- the Scene Inspector visual fixture is updated.

### 14.7 Regression

Keep green:

- Scene Layer add/rename/reorder/remove;
- layer locking;
- active layer selection;
- workspace hidden layers;
- tilemap and sprite interleaving;
- camera follow and camera clamp;
- grid, snap, picking, and transform gizmos;
- canonical save/reload;
- Play materialization;
- Windows and WASM runtime builds.

---

## 15. Implementation Order

1. Add ADR-0056.
2. Add pure default/finite helpers for `SceneLayerSettings`.
3. Add `ProjectDocument::effectiveLayerSettings`.
4. Add `ProjectDocument::setSceneLayerParallax`.
5. Make layer removal erase settings atomically.
6. Extend `RemoveSceneLayerCommand` undo state.
7. Add `SetSceneLayerParallaxCommand`.
8. Restore complete `layerSettings` serialization.
9. Add validation for keyed layer settings and numeric fields.
10. Add document, command, lifecycle, and persistence tests.
11. Add Layer Manager fields and existing action routing.
12. Add UI validation and visual-harness coverage.
13. Correct screen-space text/gauge handling in the runtime entity render pass.
14. Add runtime render-math tests.
15. Verify Play through the canonical loader.
16. Refresh runtime templates/build identity only when vendored runtime binaries changed.

---

## 16. Rejected Alternatives

### 16.1 Scene-wide parallax

Rejected because different layers require independent depth behavior.

### 16.2 Parallax in Scene Appearance

Rejected because Scene Appearance is scene-global while parallax is layer-specific.

### 16.3 Parallax in each entity Inspector

Rejected because the render layer is the ownership boundary. Per-instance fields would duplicate the same setting across many placements.

### 16.4 New Parallax component

Rejected because the canonical project and runtime already have layer parallax.

### 16.5 New ParallaxManager

Rejected because the feature is a pure render projection over existing scene data. It has no independent lifecycle or authority.

### 16.6 Always-on Edit preview

Rejected for this slice because it would make authored geometry, displayed geometry, picking, and gizmo coordinates diverge unless the complete editor interaction model were projected.

### 16.7 Reusing the Layer Manager eye icon

Rejected because the eye is workspace-only editor declutter, while `SceneLayerSettings::visible` is persistent runtime presentation.

---

## 17. Consequences

### Positive

- The native editor can author a runtime feature that already exists.
- Save/reopen no longer discards valid `layerSettings`.
- Play and exported builds consume the same canonical data.
- Layer identity/order ownership remains unchanged.
- Undo/Redo and dirty tracking remain command-based.
- No second manager, renderer, or synchronization system is introduced.
- Normal authoring coordinates remain predictable.

### Trade-offs

- Parallax is previewed through Play rather than directly in the normal Scene View.
- The Layer Manager becomes slightly denser and requires a clear active-layer settings subsection.
- The writer must preserve dormant `SceneLayerSettings` fields not yet exposed by the UI.
- Screen-space text/gauge behavior requires a focused runtime correction.

---

## 18. Acceptance Criteria

The slice is complete when all of the following are true:

```text
1. A user selects a scene layer in the Layer Manager.
2. The Inspector shows X/Y parallax factors for that active layer.
3. Committing a finite value executes one undoable command.
4. ProjectDocument is the only mutated authoring authority.
5. Save/reopen preserves the factors and every existing layer-settings field.
6. Play receives the factors only through canonical project loading.
7. Runtime sprites, entity-owned tilemaps, and world-space UI move with the layer factor.
8. Screen-space UI does not move with parallax.
9. Physics, collision, transforms, Logic, and camera targets are unchanged.
10. A factor of 1.00 is pixel-equivalent to existing behavior.
11. Locked layers remain authorable at the layer-settings level.
12. The workspace layer eye remains editor-only.
13. Undo/Redo, dirty revision, and layer removal/restoration are exact.
14. No new manager, parallel model, or hidden synchronization path exists.
```
