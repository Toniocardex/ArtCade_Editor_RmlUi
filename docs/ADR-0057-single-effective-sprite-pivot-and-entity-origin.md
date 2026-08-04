# ADR-0057 — Single Effective Sprite Pivot and Entity Origin

**Status:** Accepted  
**Date:** 2026-08-02  
**Related:** ADR-0001, ADR-0014, ADR-0049, ADR-0056  
**Scope:** Canonical sprite-pivot authoring for Object Types and optional EntityInstance overrides; static and animated sprite alignment; shared runtime/editor geometry; persistence, migration, commands, Inspector UX, Scene View bounds, picking, and transform-gizmo parity.  
**Out of scope:** Per-asset runtime pivot fallback, per-animation pivot, per-clip pivot, per-frame pivot, trimmed-frame metadata, entity/physics origin authoring, child transforms, sockets/image-point authoring, runtime Logic mutation of pivot, and a dedicated draggable Pivot tool.

**Accepted amendment (2026-08-04) — alpha-tight Edit authoring bounds:** the
rendered sprite rectangle and canonical Sprite Pivot remain unchanged. Scene View
derives manipulation geometry from the smallest non-transparent pixel rectangle
inside the current source frame. Outline, picking, and transform gizmo consume
that same derived rectangle. The derived Entity Origin location within it may be
outside `[0, 1]`; it is not a second pivot authority and is never persisted.
Missing/undecodable or fully transparent pixels fall back explicitly to the full
rendered frame. Play and exported rendering are unchanged.

---

## 1. Context

ArtCade currently has one canonical gameplay transform:

```cpp
struct Transform {
    Vec2  position;
    Vec2  scale = {1.f, 1.f};
    float rotation = 0.f;
    Vec2  velocity;
};
```

`Transform::position` is the entity's world-space gameplay origin. Movement, physics, collision, camera targeting, Logic, spawning, scene bounds, and rendering already depend on it.

The runtime also contains partial pivot fields:

```cpp
struct SpriteComponent {
    bool pivotFromAsset = true;
    Vec2 pivot = {0.5f, 0.5f};
};
```

and legacy/import metadata:

```cpp
struct ImageAssetDef {
    Vec2 defaultPivot = {0.5f, 0.5f};
};
```

The renderer already accepts a normalized pivot for static images and animation frames. The current canonical v12 authoring model, however, does not place one explicit authoritative pivot on `SpritePresentationComponent`, while editor geometry still assumes that `Transform.position` is the sprite's geometric center.

A naïve design could create this precedence chain:

```text
Image Asset pivot
→ Animation Asset pivot
→ Clip pivot
→ Frame pivot
→ Object Type pivot
→ EntityInstance pivot
```

That design is rejected. It would create multiple authoring locations capable of moving the same visual result and would make debugging, migration, and editor/runtime parity unnecessarily difficult.

---

## 2. Decision Summary

ArtCade will maintain exactly two distinct concepts:

```text
Transform.position
→ canonical gameplay origin

effective SpritePresentation.pivot
→ canonical visual origin of the sprite
```

The sprite pivot is one property resolved through the existing Object Type / EntityInstance inheritance model:

```text
ObjectType SpritePresentation.pivot
→ optional SceneInstance SpritePresentation override
→ one effective pivot
```

Static images and every frame of an animation use the same effective pivot.

The complete flow is:

```text
ObjectType.spritePresentation.pivot
        ↓
optional SceneInstance.spritePresentationOverride.pivot
        ↓
resolveEffectiveSpritePresentation()
        ↓
runtime SpriteComponent.pivot / editor SpriteRenderView.pivot
        ↓
resolveSpriteVisualGeometry()
        ↓
renderer / Scene View / picking / outline / gizmo
```

Only the Object Type value and optional instance override are persistent authoring data. Every later value is derived.

---

## 3. Architectural Invariants

### 3.1 One gameplay origin

`Transform::position` remains the sole world-space gameplay origin.

Changing the sprite pivot must not modify:

- position or velocity;
- BoxCollider2D offsets or bounds;
- CollisionWorld shapes;
- physics body positions;
- Platformer ground support;
- camera targets;
- Logic Board coordinates;
- tilemap origins;
- Text/Gauge offsets;
- scene containment data.

### 3.2 One effective visual pivot

The only resolution rule is:

```cpp
effectivePivot =
    instance.spritePresentationOverride.pivot.value_or(
        objectType.spritePresentation.pivot);
```

No asset, animation, clip, frame, renderer, or editor fallback participates after canonical migration.

### 3.3 One shared resolver

Editor and runtime consume the same pure presentation resolver. They must not independently reimplement inheritance, override precedence, or pivot defaults.

### 3.4 One shared geometry formula

Runtime rendering, editor rendering, bounds, picking, outlines, and transform gizmos consume one pure sprite-geometry projection. Duplicated pivot math is prohibited.

The Edit-only alpha-tight rectangle is a derived crop of that projection, using
the decoded source image plus the current source rectangle as its sole authority.
It changes neither destination rendering nor the authored pivot.

### 3.5 Animation does not own alignment

The SpriteAnimator chooses the animation asset, clip, frame index, source rectangle, and playback state. It does not choose the pivot.

### 3.6 Pivot is presentation-only

The sprite may visually move around the entity origin when the pivot changes. Gameplay geometry does not move.

---

## 4. Terminology

### Entity Origin

The world-space point stored in `Transform::position`. It is the gameplay origin and the point around which the sprite rotates and scales.

### Sprite Pivot

A normalized point in the unflipped sprite rectangle:

```text
(0.0, 0.0) → top-left
(0.5, 0.5) → center
(0.5, 1.0) → bottom-center
(1.0, 1.0) → bottom-right
```

The selected point is placed at the Entity Origin.

### Effective Pivot

The single pivot obtained after Object Type inheritance and optional instance override.

### Geometric Center

The center of the rendered rectangle. It may differ from the Entity Origin.

---

## 5. Canonical Persistent Model

Extend the existing Sprite Presentation model:

```cpp
struct SpritePresentationComponent {
    bool                     visible = true;
    SpritePresentationSource source = SpritePresentationNone{};

    // Normalized visual origin and Object Type default.
    Vec2 pivot = {0.5f, 0.5f};
};

struct SpritePresentationOverride {
    std::optional<bool>                     visible;
    std::optional<SpritePresentationSource> source;

    // Sparse per-instance replacement.
    std::optional<Vec2> pivot;
};
```

No pivot field is added to:

- `SpritePresentationImage`;
- `SpritePresentationAnimation`;
- `SpriteAnimationAssetDef`;
- `SpriteAnimationClipDef`;
- `SpriteFrameDef`.

### 5.1 Ownership

The Sprite Presentation owns the pivot because it owns the entity's selected static or animated visual source.

An animation asset remains reusable by Object Types with different alignment needs.

### 5.2 Instance override

The instance override is not a second authority. It is the existing sparse inheritance mechanism. Every consumer receives only the resolved value.

---

## 6. Pivot Domain and Validation

The first implementation uses normalized coordinates in the closed range:

```text
0.0 <= pivot.x <= 1.0
0.0 <= pivot.y <= 1.0
```

Both values must be finite.

Invalid values are rejected, never silently clamped:

```text
NaN
+Infinity
-Infinity
x or y below 0
x or y above 1
```

External pivots require a future explicit decision.

---

## 7. Shared Effective Presentation Resolver

Introduce a UI-free and renderer-free resolver in shared core code.

Suggested files:

```text
vendor/artcade-runtime/src/core/sprite-presentation-resolve.h
vendor/artcade-runtime/src/core/sprite-presentation-resolve.cpp
```

Suggested projection:

```cpp
struct EffectiveSpritePresentation {
    bool                     present = false;
    bool                     visible = true;
    SpritePresentationSource source = SpritePresentationNone{};
    Vec2                     pivot = {0.5f, 0.5f};
};

EffectiveSpritePresentation resolveEffectiveSpritePresentation(
    const EntityDef& objectType,
    const SceneInstanceDef& instance);
```

Contract:

1. no Object Type Sprite Presentation:
   - `present = false`;
   - an instance presentation override is invalid;
2. copy Object Type `visible`, `source`, and `pivot`;
3. replace only instance fields whose optionals are set;
4. return one complete projection.

The resolver must not:

- access `ProjectDocument`;
- search assets;
- inspect animation frames;
- read `ImageAssetDef::defaultPivot`;
- consult `SpriteComponent::pivotFromAsset`;
- allocate resources;
- mutate inputs.

The editor may derive `ComponentOrigin` metadata separately for labels. That metadata never participates in value resolution.

---

## 8. Legacy Runtime Fields

### 8.1 SpriteComponent::pivot

`SpriteComponent::pivot` remains a transient runtime field populated from the effective presentation:

```cpp
runtimeSprite.pivot = effectivePresentation.pivot;
```

### 8.2 SpriteComponent::pivotFromAsset

`pivotFromAsset` is deprecated from the canonical path:

```cpp
runtimeSprite.pivotFromAsset = false;
runtimeSprite.pivot = effectivePresentation.pivot;
```

No current-format renderer or materializer may branch on it.

### 8.3 ImageAssetDef::defaultPivot

`ImageAssetDef::defaultPivot` is not a live runtime fallback.

It may remain as legacy/import metadata or as a value copied once when creating a new Sprite Presentation.

Changing it must not move existing Object Types or instances.

---

## 9. Persistence and Schema Version

The project format advances:

```text
formatVersion 12
→ formatVersion 13
```

No Logic Board schema/API bump is required.

### 9.1 Object Type JSON

The v13 writer always emits the Object Type pivot:

```json
{
  "spritePresentation": {
    "visible": true,
    "source": {
      "kind": "animation",
      "assetId": "hero-animation",
      "defaultClipId": "idle",
      "autoPlay": true,
      "playbackSpeed": 1.0
    },
    "pivot": {
      "x": 0.5,
      "y": 1.0
    }
  }
}
```

### 9.2 Instance override JSON

The writer emits an instance pivot only when overridden:

```json
{
  "spritePresentationOverride": {
    "pivot": {
      "x": 0.0,
      "y": 0.5
    }
  }
}
```

Reset removes only `pivot` and preserves independent `visible` and `source` overrides. If no optional override remains, erase the complete override object.

### 9.3 Strict v13 reading

For a v13 Object Type with Sprite Presentation, `pivot` is required and must be a finite normalized Vec2.

An instance override pivot is optional, but when present follows the same validation.

Malformed v13 input fails loading.

---

## 10. Migration

### 10.1 v12 to v13

Add this explicit value to every Object Type Sprite Presentation:

```cpp
pivot = {0.5f, 0.5f};
```

No instance pivot override is created.

### 10.2 Older legacy formats

When a legacy migration still exposes old pivot fields:

```text
pivotFromAsset == false
→ copy legacy SpriteComponent.pivot

pivotFromAsset == true
→ copy the safely resolvable legacy asset default
→ otherwise center
```

The result is written once into `SpritePresentationComponent::pivot`. Legacy fields then leave the current-format path.

### 10.3 Migration invariant

After migration:

```text
no live asset-pivot dependency
no live pivot precedence chain
one explicit Object Type pivot
```

---

## 11. Runtime Materialization

The Object Type / instance materializer calls the shared resolver.

Conceptually:

```cpp
const EffectiveSpritePresentation effective =
    resolveEffectiveSpritePresentation(objectType, instance);

runtimeSprite.pivotFromAsset = false;
runtimeSprite.pivot = effective.pivot;
```

Dynamic spawn uses the Object Type pivot. Authored scene placement uses the instance override when present.

Pivot is not mutable through Logic Board, manual Lua, or gameplay host APIs in this slice.

---

## 12. Animation Contract

Every frame uses the same effective pivot.

`SpriteAnimator::Frame` remains source-rectangle data only:

```cpp
struct Frame {
    int x;
    int y;
    int w;
    int h;
};
```

No pivot is added to the animator frame or `ResolvedSpriteDraw` when the runtime snapshot already carries `SpriteComponent::pivot`.

The draw path passes the materialized value:

```cpp
renderer.drawSpriteFrame(
    sheet,
    frame.x,
    frame.y,
    frame.w,
    frame.h,
    renderedEntityOrigin,
    transform.rotation,
    transform.scale,
    sprite.tint,
    alpha,
    sprite.pivot,
    sprite.flipX,
    sprite.flipY);
```

A bottom-center pivot keeps the bottom center of differently sized frames on the same Entity Origin.

Per-frame pivot correction is rejected. Future tightly trimmed frames must use technical `logicalCanvasSize` and `trimOffset` metadata under a separate ADR.

---

## 13. Shared Sprite Visual Geometry

Introduce one pure helper:

```text
vendor/artcade-runtime/src/core/sprite-visual-geometry.h
vendor/artcade-runtime/src/core/sprite-visual-geometry.cpp
```

```cpp
struct SpriteVisualGeometry {
    Vec2 anchorWorld{};
    Vec2 size{};
    Vec2 effectivePivot{};
    Vec2 originPixels{};
    Vec2 unrotatedTopLeft{};
    Vec2 visualCenter{};
    float rotationRadians = 0.f;
};

SpriteVisualGeometry resolveSpriteVisualGeometry(
    Vec2 anchorWorld,
    float rotationRadians,
    Vec2 scale,
    Vec2 unscaledFrameSize,
    Vec2 pivot,
    bool flipX,
    bool flipY);
```

Formula:

```cpp
size = {
    unscaledFrameSize.x * abs(scale.x),
    unscaledFrameSize.y * abs(scale.y)
};

effectivePivot = {
    flipX ? 1.f - pivot.x : pivot.x,
    flipY ? 1.f - pivot.y : pivot.y
};

originPixels = {
    effectivePivot.x * size.x,
    effectivePivot.y * size.y
};

unrotatedTopLeft = anchorWorld - originPixels;

centerOffsetLocal = {
    size.x * 0.5f - originPixels.x,
    size.y * 0.5f - originPixels.y
};

visualCenter =
    anchorWorld + rotate(centerOffsetLocal, rotationRadians);
```

Consumers:

- runtime renderer;
- Scene View draw;
- oriented bounds and AABB;
- picking;
- selection outline;
- pivot marker;
- resize-gizmo geometry;
- visual tests.


---

## 14. Rendering

### 14.1 Static sprite

The entity origin passed to the renderer is the post-parallax render anchor. The shared geometry helper derives destination size and origin.

### 14.2 Animated sprite

The current frame width and height use the same geometry helper and effective pivot. Changing clip or frame changes the source rectangle, never the pivot authority.

### 14.3 Placeholder rendering

An entity without a drawable Sprite Presentation continues to use the generic centered placeholder.

Sprite Pivot does not affect:

- placeholders;
- entity-owned tilemaps;
- Text;
- Gauge;
- collision overlays.

### 14.4 Parallax composition

The order is:

```text
authored Transform.position
→ layer-parallax projection
→ rendered Entity Origin
→ sprite-pivot geometry
```

### 14.5 Text and Gauge

Text and Gauge offsets remain relative to `Transform.position`, following their existing world/screen-space policy. They do not follow the sprite's geometric center.

---

## 15. Editor Projection

Extend the editor projection:

```cpp
struct SpriteRenderView {
    bool            present = false;
    bool            visible = false;
    AssetId         assetId;
    AssetId         animationAssetId;
    AnimationFrameRect sourceRect{};
    bool            hasSourceRect = false;

    Vec2            pivot = {0.5f, 0.5f};
    ComponentOrigin pivotOrigin = ComponentOrigin::None;

    // existing diagnostics and origin fields...
};
```

`resolveSpriteRenderer()` obtains the value through the shared effective-presentation resolver. It must not query asset pivot metadata.

For animation presentation in Edit mode, the existing default-clip preview frame supplies dimensions; the pivot remains the effective Sprite Presentation pivot.

If the source is `none`, the Inspector may retain the pivot, but no sprite geometry is produced.

---

## 16. Scene View Geometry and Picking

The current centered assumption is replaced for sprites only.

The generic transform projection remains valid for placeholders and other centered editor visuals.

### 16.1 Bounds

The oriented sprite bounds use:

```text
center = geometry.visualCenter
size = geometry.size
rotation = transform.rotation
```

The AABB is derived from that oriented rectangle.

### 16.2 Picking

Picking must use the same oriented rectangle that is rendered.

### 16.3 Selection outline

The outline follows the pivot-aware bounds and must not remain centered on `Transform.position`.

### 16.4 Pivot marker

For a selected drawable sprite, show a non-draggable marker at:

```cpp
Transform::position
```

Suggested visual:

- small filled circle;
- thin crosshair;
- optional line to the geometric center when they differ.

---

## 17. Transform Gizmo

The Transform remains the only mutated geometry.

### 17.1 Move

Move changes `Transform::position`. The sprite follows because the pivot anchor is that position.

### 17.2 Rotation

Rotation occurs around `Transform::position`.

### 17.3 Scale

The resize solver must use the pivot-aware local rectangle:

```text
left   = -effectivePivot.x * width
right  = (1 - effectivePivot.x) * width
top    = -effectivePivot.y * height
bottom = (1 - effectivePivot.y) * height
```

To preserve the existing opposite-handle behavior:

1. transform the dragged point into unrotated local axes;
2. keep the opposite edge or corner fixed;
3. solve new width and height;
4. derive new scale;
5. derive the new Entity Origin from the new rectangle and effective pivot;
6. commit position and scale atomically through `SetEntityTransformCommand`.

For the new local rectangle:

```cpp
anchorLocal = {
    left + effectivePivot.x * width,
    top + effectivePivot.y * height
};
```

The gizmo never writes the pivot during resize.

Existing restrictions for scaling rotated entities remain unchanged unless another ADR broadens them.

---

## 18. Authoring Commands

No `PivotManager` is introduced.

### 18.1 Object Type pivot

```cpp
class SetObjectTypeSpritePivotCommand final : public EditorCommand {
public:
    SetObjectTypeSpritePivotCommand(
        ObjectTypeId objectTypeId,
        Vec2 pivot);

    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;

private:
    ObjectTypeId objectTypeId_;
    Vec2 next_{};
    Vec2 previous_{};
    bool captured_ = false;
};
```

Contract:

- Object Type must exist;
- Sprite Presentation must exist;
- pivot must be finite and normalized;
- unchanged value is a no-op;
- first apply captures the previous value;
- one document revision;
- exact Undo/Redo;
- Object Type domain change;
- Inspector and Viewport invalidation.

### 18.2 Instance override

Use one typed command whose optional value represents set/reset:

```cpp
class SetInstanceSpritePivotOverrideCommand final : public EditorCommand {
public:
    SetInstanceSpritePivotOverrideCommand(
        SceneId sceneId,
        EntityId entityId,
        std::optional<Vec2> pivotOverride);

    EditorOperationResult apply(ProjectDocument&) override;
    EditorOperationResult undo(ProjectDocument&) override;
};
```

Semantics:

```text
Vec2
→ set or replace the sparse override

nullopt
→ remove only the pivot override
```

Independent source and visibility overrides are preserved.

When the last optional field disappears, erase the complete `SpritePresentationOverride`.

### 18.3 Lock and Play policy

Object Type pivot:

- editable from Object Type Inspector;
- not blocked by scene-layer lock;
- blocked during Play.

Instance override:

- blocked when the instance layer is locked;
- blocked during Play;
- Undo/Redo remains reproducible if the lock changes later.

---

## 19. ProjectDocument Mutation

Add narrow mutators:

```cpp
bool setObjectTypeSpritePivot(
    const ObjectTypeId& objectTypeId,
    Vec2 pivot);

bool setInstanceSpritePivotOverride(
    const SceneId& sceneId,
    EntityId entityId,
    std::optional<Vec2> pivotOverride);
```

Each mutator:

- validates target and capability;
- changes only the pivot field;
- preserves source and visibility;
- allocates one revision;
- performs no UI invalidation itself.

### 19.1 Removing Sprite Presentation

Removing an Object Type Sprite Presentation must clear incompatible presentation overrides from every instance of that type in the same atomic operation, including pivot overrides.

### 19.2 Changing source

Changing between `None`, `Image`, and `Animation` does not change the pivot.

---

## 20. Inspector UX

### 20.1 Object Type Inspector

Inside Sprite Presentation:

```text
PIVOT

[ Top Left ] [ Top Center ] [ Top Right ]
[ Mid Left ] [ Center     ] [ Mid Right ]
[ Bot Left ] [ Bot Center ] [ Bot Right ]

X    [ 0.500 ]
Y    [ 1.000 ]

[ Reset to Center ]
```

Preset values:

| Label | Value |
|---|---|
| Top Left | `{0.0, 0.0}` |
| Top Center | `{0.5, 0.0}` |
| Top Right | `{1.0, 0.0}` |
| Middle Left | `{0.0, 0.5}` |
| Center | `{0.5, 0.5}` |
| Middle Right | `{1.0, 0.5}` |
| Bottom Left | `{0.0, 1.0}` |
| Bottom Center | `{0.5, 1.0}` |
| Bottom Right | `{1.0, 1.0}` |

Behavior:

- existing authoring-number parser and formatter;
- Enter or blur commits one command;
- Escape restores baseline;
- malformed/out-of-range input creates no command;
- no per-keystroke document mutation.

### 20.2 Instance Inspector

Inherited state:

```text
PIVOT
Inherited from Object Type: Bottom Center

[ Override Pivot ]
```

Override state:

```text
PIVOT
Instance Override

[preset grid]

X    [ 0.000 ]
Y    [ 0.500 ]

[ Reset to Object Type ]
```

The inherited value is derived and is never stored in workspace state.

### 20.3 Source changes

Changing image, animation, or default clip must not reset the pivot.

### 20.4 Help text

> The pivot is the point of the sprite placed at the entity's Position. Rotation and scale occur around this point. It affects rendering only; colliders and gameplay coordinates do not move.

---

## 21. Sprite Animation Editor

The animation asset does not own a pivot, so the editor must not expose editable asset-, clip-, or frame-pivot fields.

### 21.1 Contextual preview

When opened from an Object Type or instance, it may receive a read-only preview context:

```cpp
struct SpriteAnimationPivotPreview {
    bool available = false;
    Vec2 effectivePivot = {0.5f, 0.5f};
    ComponentOrigin origin = ComponentOrigin::None;
};
```

It may display:

```text
Preview Pivot: Bottom Center
Source: Object Type / Instance Override
```

The animation canvas keeps one preview anchor fixed and draws every frame around the same effective pivot.

### 21.2 Asset-only opening

Without entity context, the editor may use center for neutral preview. That value is not persisted or presented as authored data.

### 21.3 Onion skin

All onion-skin frames share one anchor and one contextual pivot.

---

## 22. Flip Semantics

The persisted pivot is expressed in unflipped visual coordinates.

At geometry time:

```cpp
effectivePivot.x =
    flipX ? 1.f - pivot.x : pivot.x;

effectivePivot.y =
    flipY ? 1.f - pivot.y : pivot.y;
```

Examples:

```text
left-center + Flip X
→ effective right-center destination origin

bottom-center + Flip X
→ remains bottom-center
```

Flip flags remain separate from scale. Negative authored scale migration continues to normalize magnitude and toggle flip before pivot geometry is resolved.

---

## 23. Physics, Collision, and Gameplay

Sprite Pivot has no effect on:

```text
BoxCollider2D
CollisionBody
CollisionProfile
Platformer support
TopDown movement
Physics body
Camera target
Logic position
Scene bounds checks
Spawn position
Tilemap origin
```

Collider visualization remains aligned to collider geometry.

A character may use:

```text
Transform.position = gameplay origin
Sprite pivot = bottom-center
BoxCollider offset = independent body placement
```

A visual hinge is not automatically a physics hinge. Entity/physics origin authoring is a separate future feature.

---

## 24. Image Points and Attachments

Image Points remain outside this slice.

A future world projection should use the same effective pivot:

```cpp
localPoint = {
    (point.x - effectivePivot.x) * frameWidth,
    (point.y - effectivePivot.y) * frameHeight
};
```

Then apply flip, scale, rotation, and Entity Origin translation.

Concepts remain distinct:

```text
Sprite Pivot
→ principal visual origin

Image Point
→ named attachment/socket
```

---

## 25. Domain Changes and Invalidation

Object Type pivot changes affect every instance of that Object Type and emit the existing Object Type domain change.

Instance overrides emit the existing entity/instance domain change.

No new cache manager is introduced.

Cached editor geometry is rebuilt from:

```text
effective Sprite Presentation
+ current preview frame
+ Transform
```

through existing explicit invalidation rules.

---

## 26. Tests

### 26.1 Resolver

Verify:

- Object Type center and bottom-center;
- instance override precedence;
- absent override inheritance;
- source/visibility override does not reset pivot;
- no asset lookup;
- no presentation produces no effective presentation.

### 26.2 Validation

Reject:

- NaN and infinities;
- values below 0 or above 1;
- malformed JSON;
- instance pivot override without Object Type Sprite Presentation.

### 26.3 Commands

Verify:

- Object Type command Undo/Redo;
- no-op on unchanged value;
- one revision per accepted command;
- instance set/reset preserves other overrides;
- empty override object erased;
- redo independent of selection;
- locked-layer instance edit rejected;
- Object Type edit unaffected by layer lock;
- Play blocks authoring.

### 26.4 Persistence

Verify:

- v13 always writes Object Type pivot;
- instance pivot remains sparse;
- exact round-trip;
- source changes preserve pivot;
- v12 migrates to center;
- legacy custom pivot migrates once;
- malformed v13 fails;
- no Logic schema/API bump.

### 26.5 Geometry

For a 100×50 frame:

```text
center {0.5,0.5}
→ origin {50,25}
→ visual center == anchor

bottom-center {0.5,1.0}
→ origin {50,50}
→ visual center is 25 px above anchor

top-left {0,0}
→ origin {0,0}
→ top-left == anchor
```

Also test:

- non-uniform scale;
- 90°, 180°, arbitrary rotation;
- Flip X/Y;
- combined flip and rotation;
- frame-size changes;
- AABB enclosure;
- containment matching render geometry.

### 26.6 Runtime

Verify:

- static and animated sprites use materialized pivot;
- clip/frame switch does not change pivot;
- `pivotFromAsset` is not consulted;
- dynamic spawn uses Object Type value;
- authored placement uses instance override;
- parallax moves anchor before pivot geometry;
- Text/Gauge remain on Transform position;
- collision and physics remain unchanged.

### 26.7 Scene View

Verify:

- draw and outline coincide;
- non-center pivot shifts visual bounds;
- picking follows rendered geometry;
- marker stays on Transform position;
- collider overlay does not move;
- animation preview uses same pivot;
- placeholders remain centered.

### 26.8 Transform gizmo

Verify:

- move changes position only;
- rotation occurs around pivot;
- scale preserves existing opposite-handle behavior;
- position and scale commit atomically;
- center behavior remains regression-equivalent;
- bottom-center and flip-aware resizing are correct;
- existing rotation restrictions remain unchanged.

### 26.9 Animation preview

Verify:

- Object Type and instance contextual preview;
- onion skins share one anchor;
- asset-only center is not persisted;
- no animation/frame pivot is serialized.

### 26.10 Regression

Keep green:

- Sprite Presentation inheritance;
- static image rendering;
- animation playback;
- flip actions;
- Transform Inspector/gizmo;
- BoxCollider overlays;
- Platformer and camera follow;
- layer parallax;
- Text/Gauge screen-space behavior;
- canonical save/reload;
- Play materialization;
- Windows and WASM builds.

---

## 27. Implementation Order

1. Add ADR-0057.
2. Advance format v12 to v13.
3. Add Object Type pivot and optional instance override.
4. Add strict finite/range validation.
5. Add shared effective-presentation resolver.
6. Route editor ownership resolution through it.
7. Route runtime materialization through it.
8. Materialize only the effective value into `SpriteComponent::pivot`.
9. Remove `pivotFromAsset` from the current-format path.
10. Implement migration and canonical JSON.
11. Add resolver/persistence tests.
12. Add shared sprite visual geometry.
13. Route renderer math through it.
14. Extend `SpriteRenderView`.
15. Route Scene View draw, OBB, AABB, picking, and outline through it.
16. Add pivot-aware resize-gizmo math.
17. Add Object Type command and Inspector UI.
18. Add instance override command and Inspector UI.
19. Add read-only animation preview context.
20. Update visual fixtures.
21. Run editor-core, runtime, visual, Windows, and WASM tests.
22. Refresh runtime templates/build identity when vendored binaries change.

---

## 28. Rejected Alternatives

### Asset pivot as live fallback

Rejected because asset edits would silently move all referencing entities and force asset lookup during geometry resolution.

### Animation Asset or Clip pivot

Rejected because reusable animation data must not dictate every Object Type's gameplay alignment.

### Per-frame pivot

Rejected because each frame would become capable of moving the visual origin. Future trim problems require trim metadata.

### From Asset / Custom mode

Rejected because it creates two live authorities and a runtime precedence branch.

### Pivot inside Transform

Rejected because it would affect gameplay, physics, collision, camera, Logic, and non-sprite components.

### Per-instance only

Rejected because common values would be duplicated across every placement.

### Object Type only

Rejected because exceptional placements would require duplicating the whole Object Type.

### PivotManager

Rejected because pivot has no independent lifecycle.

### Separate editor/runtime math

Rejected because render, bounds, and picking would diverge.

---

## 29. Consequences

### Positive

- one explicit pivot explains static and animated sprites;
- Object Types provide reusable defaults;
- instances have one sparse exception path;
- asset and frame changes cannot silently change alignment;
- runtime and editor share one formula;
- gameplay and physics ownership remain unchanged;
- future trim metadata remains possible without new pivot authorities.

### Trade-offs

- alpha-tight Edit bounds require a decoded source image; the documented full-frame fallback applies when pixels are unavailable or empty;
- Scene View and gizmo need pivot-aware geometry;
- projects require v13 migration;
- visual hinges do not become physics hinges.

---

## 30. Acceptance Criteria

The slice is complete when:

```text
1. Every v13 Sprite Presentation stores one explicit Object Type pivot.
2. An instance may optionally replace it through the existing sparse override.
3. One shared resolver produces the effective value.
4. Asset, animation, clip, frame, renderer, and editor cannot replace it.
5. Static images and every animation frame use the same value.
6. Transform.position remains unchanged when pivot is edited.
7. Rotation and scale operate around Transform.position.
8. Collider, physics, camera, Logic, Text, Gauge, and tilemap origins are unchanged.
9. Rendering, Scene View, bounds, picking, outline, and gizmo use one formula.
10. Flip preserves the corresponding visual anchor.
11. v13 persistence is strict and exact.
12. v12 projects migrate without visual regression.
13. Commands are undoable and revision-correct.
14. Animation Editor preview cannot author another pivot.
15. No PivotManager, per-frame pivot, asset fallback, or hidden synchronization exists.
```

---

## 31. Final Rule

```text
One entity has one gameplay origin.
One rendered sprite has one effective visual pivot.
Object Type plus optional sparse instance override resolves that one value.
Every other representation is derived.
```
