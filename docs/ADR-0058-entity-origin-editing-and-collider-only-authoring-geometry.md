# ADR-0058 — Entity Origin Editing and Collider-Only Authoring Geometry

**Status:** Proposed  
**Date:** 2026-08-03  
**Related:** ADR-0001, ADR-0014, ADR-0049, ADR-0057  
**Scope:** Scene View authoring geometry priority for entities without a drawable sprite; collider-aware picking, outline, and transform gizmo; Entity Origin marker for every selected instance; sparse per-instance `BoxCollider2D` offset override; atomic Edit Origin commands that preserve world-space collision masks.  
**Out of scope:** Changing `SpritePresentation.pivot` semantics; inventing a generic `EntityPivot` / `PivotManager`; per-instance collider size override (unless a later ADR proves need); physics-origin authoring distinct from `Transform.position`; sockets / image points; child transforms; runtime Logic mutation of origin or collider offset; draggable Pivot tool as a separate persistent tool mode beyond Edit Origin gesture; redesign of sprite unscaled size (ADR-0057 B5).

---

## 1. Context

ADR-0057 established a strict split:

```text
Transform.position
→ sole gameplay / Entity Origin

effective SpritePresentation.pivot
→ visual alignment of the sprite around that origin
```

That decision is correct and remains binding. It does **not** assign a visual pivot to entities that have no `SpritePresentation`.

After ADR-0057, a common authoring case is still wrong in the editor: an invisible solid (no sprite, `Visible = Off`, enabled `BoxCollider2D`). Hierarchy selection, Scene View outline, picking, and transform-gizmo resize still treat the instance as a generic centered **32×32 placeholder**, not as its real collision mask.

Observed symptom:

```text
solid · 4
  Visible = Off
  Sprite = absent
  BoxCollider2D = present (size / offset authored on Object Type)

→ outline and gizmo handles sit on the 32×32 placeholder
→ collider overlay may be drawn, but is not the primary manipulative geometry
→ Entity Origin marker is shown only when a drawable sprite exists
```

A naïve fix would attach `SpritePresentation.pivot` (or invent `EntityPivot`) to collider-only entities. That is rejected: a pivot without a presentation component is a property without an owner, and would create a third competing “origin” concept.

`BoxCollider2D` is Object Type authority today (ADR-0014). All instances of `solid` share the same authored collider. Per-instance origin editing that must not move the world-space mask therefore needs a **sparse instance offset override**, not a second collider authority and not a sprite pivot.

---

## 2. Decision Summary

ArtCade keeps **three separate responsibilities** and never collapses them:

```text
Transform.position
→ Entity Origin (always present; universal gameplay origin)

SpritePresentation.pivot
→ sprite alignment relative to Entity Origin
   (only when SpritePresentation exists)

BoxCollider2D.offset (+ optional instance override)
→ collider alignment relative to Entity Origin
```

They are not three competing origins:

```text
Transform.position places the entity in the world.
Sprite pivot places the sprite around that position.
Collider offset places the collider around that same position.
```

For an entity without a drawable sprite, the effective visual pivot is simply:

```text
Entity Origin = Transform.position
```

Editor geometry (outline, picking, gizmo bounds) follows a **priority**, not an unprioritized union for manipulation:

```text
1. Drawable sprite
2. Populated tilemap
3. Enabled BoxCollider2D (effective)
4. Text / Gauge (world-space)
5. Placeholder 32×32
```

Collider-relative origin authoring is performed by an **Edit Origin** operation that atomically updates:

```text
Transform.position
+ effective BoxCollider2D.offset (via Object Type or instance override)
```

so the collision mask remains fixed in world space while the local origin moves.

ADR-0057 is clarified, not overturned:

```text
Sprite Pivot is available only when SpritePresentation exists.
Transform.position is always the Entity Origin.
Entities without a drawable sprite use their effective collider,
tilemap, text, gauge, or placeholder geometry for editor bounds.
Collider-relative origin authoring is not represented by
SpritePresentation.pivot.
```

---

## 3. Architectural Invariants

### 3.1 One Entity Origin

`Transform::position` remains the sole world-space gameplay origin for movement, physics, collision world placement, camera targeting, Logic coordinates, spawning, and scene containment.

No parallel `EntityPivot`, `PivotManager`, or synthetic pivot field is introduced.

### 3.2 Sprite pivot stays presentation-only

`SpritePresentation.pivot` continues to mean sprite alignment only (ADR-0057).  
Edit Origin must not write sprite pivot.  
Absence of `SpritePresentation` means absence of sprite pivot — not a fallback to collider math disguised as pivot.

### 3.3 Collider authority remains BoxCollider2D

Persistent collision authoring remains `BoxCollider2D` (ADR-0014).  
Runtime `CollisionBody` stays derived.  
This ADR adds only a sparse **instance offset override**, not a second collider component and not an editable runtime body.

### 3.4 One effective collider for editor and runtime

Editor overlays, materialization, and collision use one resolver:

```text
ObjectType.boxCollider2D
→ optional SceneInstance.boxCollider2DOverride.offset
→ EffectiveBoxCollider2D
→ editor geometry / materialize / CollisionBody derivation
```

### 3.5 World-space mask preservation for Edit Origin

Changing Entity Origin relative to a collider must keep the collider’s world-space AABB (or oriented bounds, when rotation is supported) unchanged. That requires a coordinated pair mutation, never position-only or offset-only for the Edit Origin gesture.

### 3.6 Edit vs Play visibility

`SceneInstanceDef::visible` continues to mean gameplay / Play visibility.  
In Edit mode, authoring geometry for an enabled collider remains drawable, pickable, and gizmo-capable even when the instance is invisible in Play and has no sprite.  
Play must not suddenly draw a sprite for collider-only entities.

### 3.7 No UI→domain mutation

Inspector / Scene View emit Intent or Command.  
`ProjectDocument` mutates only through coordinator-executed commands.  
No per-frame sync from overlays into authoring.

---

## 4. Clarification to ADR-0057

ADR-0057’s title mentions “Entity Origin” because `Transform.position` is that origin. This ADR owns **editor tooling and collider-only geometry** around that origin.

Binding clarification to record against ADR-0057:

```text
1. Sprite Pivot UI and commands require SpritePresentation.
2. Entity Origin marker is not a sprite feature; it marks Transform.position
   for any selected instance.
3. Collider-only solids are not a Sprite Pivot use case.
4. Placeholder 32×32 is a last-resort editor stand-in, never preferred over
   an enabled effective BoxCollider2D when no higher-priority visual exists.
```

No change to ADR-0057 persistence, resolver, or sprite geometry formula is required for this clarification.

---

## 5. Authoring geometry priority

### 5.1 Discriminated source

```cpp
enum class TransformGeometrySource {
    Sprite,
    Tilemap,
    Collider,
    TextOrGauge,
    Placeholder,
};

struct InstanceTransformGeometry {
    TransformGeometrySource source = TransformGeometrySource::Placeholder;
    SceneFrameTransform2D bounds{};   // manipulative visual / gizmo frame
    Vec2 entityOrigin{};              // Transform.position (world)
    Vec2 unscaledSize{32.f, 32.f};
    Vec2 effectivePivot{0.5f, 0.5f};  // resize anchor in local bounds space
    bool supportsScale = false;
};
```

Semantics:

| Source | `bounds` | `effectivePivot` (resize) | Notes |
|---|---|---|---|
| Sprite | ADR-0057 `visualTransform` | sprite effective pivot | unchanged from ADR-0057 |
| Tilemap | populated cell union (unrotated, current contract) | center of that union unless a later ADR says otherwise | scale policy unchanged |
| Collider | effective BoxCollider2D world geometry | relative position of `Transform.position` inside collider bounds | primary fix for invisible solids |
| TextOrGauge | world text/gauge bounds | center (or documented anchor) | scale may remain disabled as today when text/gauge-only |
| Placeholder | 32×32 centered on Entity Origin | `{0.5, 0.5}` | last resort only |

### 5.2 Priority rule (manipulation / outline / picking)

For a given instance, choose the **first** applicable source:

1. Drawable sprite (`SpritePresentation` resolved with non-empty asset / visible presentation draw path used by Scene View today)
2. Populated tilemap (one or more painted cells)
3. Enabled effective `BoxCollider2D`
4. World-space Text or Gauge with measurable bounds
5. Placeholder 32×32

**Containment** (`editorBoundsForEntity`) may still union content for scene-edge warnings when multiple representations exist (sprite ∪ collider), but **gizmo, selection outline, and primary pick target** must follow the priority above so the placeholder cannot dominate a larger collider.

### 5.3 Picking

`pickEntityAt` must hit the collider world geometry when the instance’s primary source is `Collider` (and when a higher-priority visual did not already claim the entity).  
A click on the collider of an invisible solid must select that instance.  
A click only on the old 32×32 placeholder region that lies **outside** the collider must miss when the primary source is `Collider`.

### 5.4 Outline

Selection outline for collider-primary entities follows the effective collider frame (oriented if/when collider rotation is supported; axis-aligned under the current BoxCollider2D contract).

### 5.5 Origin marker

For **every** selected instance in Edit mode, Scene View draws a non-authoring marker at `Transform.position`:

```text
● = Entity Origin (Transform.position)
□ = primary authoring geometry (sprite / tilemap / collider / …)
```

The marker is not limited to sprites. It is not a second hit target that steals picks from the primary geometry unless an Edit Origin drag gesture is active.

---

## 6. Sparse BoxCollider2D instance override

### 6.1 Model

```cpp
struct BoxCollider2DOverride {
    std::optional<Vec2> offset;
};

// SceneInstanceDef
std::optional<BoxCollider2DOverride> boxCollider2DOverride;
```

`empty()` / erase rules:

```text
override absent OR offset absent → inherit Object Type offset
override with offset set → replace offset only
size / enabled / mode remain Object Type authority in this ADR
```

### 6.2 Resolver

```cpp
struct EffectiveBoxCollider2D {
    bool present = false;
    BoxCollider2DComponent value{}; // offset already resolved
};

EffectiveBoxCollider2D resolveEffectiveBoxCollider2D(
    const EntityDef& objectType,
    const SceneInstanceDef& instance);
```

Rules:

- no OT `boxCollider2D` → not present (instance override alone is invalid / rejected by validation)
- OT present → start from OT component
- if instance override carries `offset`, replace `value.offset`
- enabled/size/mode always from OT in this slice

### 6.3 Why only offset

Edit Origin needs to re-express the same world mask in a new local frame. That is exactly an offset change (plus `Transform.position`).  
Per-instance size is deferred until a concrete product need appears.

### 6.4 Materialization

`materializeInstance` / Play / export must consume the **effective** collider (OT + optional offset override) before deriving `CollisionBody`.  
No editor-only collision path.

### 6.5 Persistence / format

This ADR introduces a schema bump only if persistence of `boxCollider2DOverride` is included in the same slice (expected: yes).

Proposed policy (to confirm at acceptance):

```text
formatVersion N → N+1
- optional scenes[].instances[].boxCollider2DOverride.offset
- absent override means inherit
- raw validation: offset finite; override requires OT BoxCollider2D
- document validation: same
- older projects: no instance overrides; behavior unchanged
```

Exact version number is chosen at implementation time from the then-current constant (today ADR-0057 left the project at **13**).

---

## 7. Edit Origin

### 7.1 Intent

Move Entity Origin relative to the effective collider without moving the collision mask in the world.

### 7.2 UI (Inspector / selection affordance)

Suggested grouping (wording can be refined in implementation):

```text
ENTITY ORIGIN

[ Top Left ] [ Top Center ] [ Top Right ]
[ Mid Left ] [ Center     ] [ Mid Right ]
[ Bot Left ] [ Bot Center ] [ Bot Right ]

[ Edit Origin ]
```

Presets are normalized anchors on the **effective collider** local box, not sprite pivots.

Availability:

- enabled when the selected instance’s Object Type has enabled `BoxCollider2D`
- instance override path when editing a scene instance
- Object Type path (optional in the same ADR or a follow-up) may adjust OT offset + warn that all instances share it; **per-instance Edit Origin must write the sparse override**, never silently mutate OT when an instance is selected

### 7.3 Atomic command

```cpp
enum class NormalizedAnchor {
    TopLeft, TopCenter, TopRight,
    MidLeft, Center, MidRight,
    BotLeft, BotCenter, BotRight,
};

class SetInstanceOriginFromColliderAnchorCommand final : public EditorCommand {
public:
    SetInstanceOriginFromColliderAnchorCommand(
        SceneId sceneId,
        EntityId entityId,
        NormalizedAnchor anchor);
    // Overlay drag variant supplies an explicit world-space origin instead
    // of a preset enum (same capture/commit shape).
};
```

Captured state (exact Undo/Redo):

```text
previousTransform / nextTransform
previousColliderOffsetOverride / nextColliderOffsetOverride
```

(Use `std::optional<Vec2>` for override absence vs present.)

### 7.4 Shared helper (mandatory)

UI must not reimplement the math. One pure helper owns:

```text
inputs:
  current Transform
  effective BoxCollider2D (size, offset, enabled)
  selected anchor (normalized or explicit world point)
  scale (and rotation when supported)

outputs:
  next Transform.position
  next collider offset (for override or OT write path)
```

Conceptual formula:

```text
newTransformPosition =
    worldPositionOfSelectedColliderAnchor;

newColliderOffset =
    oldColliderOffset - selectedAnchorLocalPosition;
```

Equivalently: choose the new origin so that the world-space collider corners remain unchanged.

Constraints:

- size unchanged
- scale contract unchanged (existing instance scale applies as today)
- rotation: follow current BoxCollider2D editor/runtime contract; if rotation is not yet applied to collider overlays, document that Edit Origin is axis-aligned until collider rotation parity exists
- single document revision / single undo step

### 7.5 Gesture separation

| Gesture | Mutates |
|---|---|
| Move gizmo body | `Transform.position` only (collider follows in world) |
| Resize gizmo | `Transform.scale` (existing contract) around captured resize pivot |
| Edit Origin (preset or drag) | `Transform.position` **and** collider offset override together |

Resize must not silently rewrite offset.  
Edit Origin must not silently rewrite scale.

### 7.6 Layer lock and Play

- instance offset override: layer lock checked on first apply only (same policy as ADR-0057 instance pivot override)
- Play: authoring blocked at `EditorCoordinator` (commands do not know `PlaySession`)
- OT collider edits remain independent of instance layer lock; instance Edit Origin is subject to the instance’s layer lock

---

## 8. Scene View / gizmo impact

### 8.1 `resolveInstanceTransformGeometry`

Must return collider-primary geometry when priority selects `Collider`, including:

- `bounds` from effective collider world rect / transform
- `entityOrigin = Transform.position`
- `effectivePivot` such that resize anchors keep Entity Origin semantics consistent with the chosen bounds (document the exact convention in implementation notes; default: pivot = location of Entity Origin inside the collider frame)

### 8.2 Collider overlay

Enabled colliders remain visible in Edit even when `Visible = Off` and no sprite is present (dimming policy may match other invisible-instance affordances, but must not remove hit/gizmo geometry).

### 8.3 No second hit via placeholder

When primary source is `Collider`, placeholder bounds are not a second pick/outline target (same spirit as ADR-0057 B4 for sprites).

---

## 9. Object Type vs instance

```text
solid
├─ solid · 1
├─ solid · 2
├─ solid · 3
└─ solid · 4
```

Shared OT collider:

```text
edit ObjectType.boxCollider2D.offset
→ all instances’ relative collider alignment change
```

Per-instance Edit Origin on `solid · 4`:

```text
writes SceneInstance.boxCollider2DOverride.offset
+ that instance’s Transform.position
→ only solid · 4’s local frame changes
→ world-space masks of ·1..·3 unchanged
→ world-space mask of ·4 unchanged
```

Inspector must make provenance obvious (Inherited vs Overridden), analogous to sprite presentation overrides.

---

## 10. Persistence, validation, migration

### 10.1 Raw JSON

- `boxCollider2DOverride` optional object on instances
- if present, `offset` optional `{x,y}` finite numbers
- override without OT `boxCollider2D` → reject
- unknown keys: follow existing project JSON policy

### 10.2 Document validation

- instance override requires OT with `boxCollider2D`
- empty override erased (do not persist useless objects)

### 10.3 Migration

- projects without overrides: identity migration / format bump only
- no automatic creation of overrides
- no rewrite of OT offsets during load

### 10.4 Export / Play preflight

Effective collider (including override) must be what export and Play materialize.  
Canonical JSON after bump must validate under `validate_current_project_json`.

---

## 11. Commands and mutators

Minimum surface:

```text
SetInstanceBoxColliderOffsetOverrideCommand
  — set / clear sparse offset override (no transform change)

SetInstanceOriginFromColliderAnchorCommand
  — atomic position + offset override

(Optional same slice or follow-up)
SetObjectTypeBoxColliderOffsetCommand
  — OT-only offset edit (existing or thin wrapper); not per-instance
```

Document mutators live on `ProjectDocument`; commands stay UI-free.

Removing OT `BoxCollider2D` must clear incompatible instance collider overrides atomically (mirror ADR-0057 presentation removal).

---

## 12. Implementation order (non-binding guide)

1. ADR accepted  
2. Geometry priority + `TransformGeometrySource` in snapshot/gizmo/picking/outline  
3. Origin marker for all selections  
4. Model `BoxCollider2DOverride` + resolver + validation + format bump  
5. Wire materialize / Play / export to effective collider  
6. Atomic Edit Origin helper + command  
7. Inspector presets + override provenance  
8. Optional origin-marker drag gesture  
9. Tests + fixture/goldens  

Slices 2–3 already fix most invisible-solid UX **without** new persistent data and can ship first if review prefers an incremental landing.

---

## 13. Testing matrix (blocking)

```text
- Collider-only instance: outline, pick, and gizmo use collider bounds; placeholder is not primary
- Visible = Off + no sprite + enabled collider: Edit still picks/outlines collider; Play draws no sprite
- Sprite present: sprite remains higher priority than collider for gizmo/outline/pick
- Populated tilemap without sprite: tilemap remains above collider
- editorBoundsForEntity containment still accounts for collider (and sprite when present)
- Origin marker drawn at Transform.position for collider-only and sprite entities
- resolveEffectiveBoxCollider2D: inherit / override / missing OT rejected
- Edit Origin Bottom Center: world collider AABB unchanged; position and override update; Undo exact
- Edit Origin on one of N shared-OT solids: only that instance’s override/position change
- Layer lock blocks first apply of instance origin/override; redo does not re-check
- Play blocks origin/override authoring at coordinator
- Remove OT BoxCollider2D clears instance collider overrides atomically; Undo restores
- format bump: old projects load; save emits new version; invalid override rejected
- No SpritePresentation invented for collider-only solids
- No Logic Board revision bump from origin edits unless Logic data actually changes
```

---

## 14. Alternatives considered

### 14.1 Reuse SpritePresentation.pivot for solids

Rejected. Property without presentation component; contaminates ADR-0057; implies a drawable that does not exist.

### 14.2 Generic EntityPivot field

Rejected. Duplicates `Transform.position` or fights collider offset; creates competing authority.

### 14.3 Always mutate Object Type offset from instance selection

Rejected. Breaks multi-instance solids that need distinct local origins; violates instance-scoped Edit Origin.

### 14.4 Per-instance full BoxCollider2D copy

Rejected for this slice as oversized. Offset override is enough for origin editing; size override needs a separate product case.

### 14.5 Keep placeholder as gizmo, collider as overlay only

Rejected. Matches the current bug: authors manipulate a phantom 32×32 instead of the mask they care about.

---

## 15. Consequences

### Positive

- invisible collision solids become first-class Edit citizens
- ADR-0057 sprite pivot stays clean
- Entity Origin remains universally `Transform.position`
- per-instance origin without forking OT collider size/mode
- atomic Undo for mask-preserving origin edits

### Trade-offs

- format bump and validation surface grow slightly
- Inspector must explain Inherited vs Overridden collider offset
- gizmo/picking priority becomes an explicit policy that tests must lock
- OT Edit Origin (shared) vs instance Edit Origin (override) must be clearly separated in UX

### Follow-ups (explicitly not this ADR)

- per-instance collider size/mode overrides
- collider rotation parity with transform rotation (if not already complete)
- sockets / image points
- dedicated persistent Pivot tool mode beyond Edit Origin

---

## 16. Acceptance Criteria

The slice is complete when:

```text
1. Transform.position remains the only Entity Origin.
2. SpritePresentation.pivot is unchanged and still requires SpritePresentation.
3. Editor geometry priority is Sprite > Tilemap > Collider > Text/Gauge > Placeholder.
4. Collider-only instances use collider bounds for outline, picking, and gizmo.
5. Placeholder is never primary when an enabled effective collider exists and no higher source applies.
6. Entity Origin marker is shown for every selected Edit instance.
7. SceneInstance may sparsely override BoxCollider2D.offset only.
8. One resolver produces EffectiveBoxCollider2D for editor and runtime.
9. Edit Origin atomically updates position + offset override and preserves world-space mask.
10. Shared pure helper owns the origin math; UI does not duplicate it.
11. Commands are undoable, revision-correct, Play-blocked, and layer-lock correct.
12. Removing OT BoxCollider2D clears incompatible instance overrides atomically.
13. Persistence/validation/migration for the override are strict.
14. No EntityPivot, PivotManager, or sprite pivot assigned to collider-only entities.
15. Play remains invisible for solids without drawable sprites.
```

---

## 17. Final Rule

```text
One entity has one Entity Origin: Transform.position.

Sprite pivot aligns a sprite to that origin.
Collider offset aligns a collider to that origin.
Instance offset override is the only sparse exception for a single placed solid.

Editor geometry prefers real content over the 32×32 placeholder.
Edit Origin moves the local frame, not the world-space collision mask.
```
