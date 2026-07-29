# ADR-0043 — Logic Board Outside Scene Bounds

**Status:** Accepted
**Date:** 2026-07-29
**Scope:** Logic Board condition `scene.outside_bounds`, `IGameplayRuntimeHost`,
Logic runtime binding/codegen, native Logic Board catalog and Play/export parity
**Related:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§18/§30,
`LOGIC_BOARD_RULES_ROADMAP.md`, ADR-0041 (per-action execution modes)

## Context

The Logic Board can destroy, spawn, transition scenes, update state, and play
feedback, but it has no rule predicate for an entity leaving the active scene.
Authors therefore cannot express the common lifecycle recipe:

```text
WHEN Outside Scene, margin 32
THEN Destroy Self, Once per activation
```

The runtime, not RmlUi or the authored document, is authoritative for an
entity's materialized transform and the active scene's world size. The editor
must not derive this predicate from sprite, collider, tilemap, texture, pivot,
or editor placeholder bounds.

## Decision

### Block contract

Add a descriptor with the following stable contract:

| Item | Value |
|---|---|
| typeId | `scene.outside_bounds` |
| Display name | Outside Scene |
| Category | Scene |
| Kind | Condition, eligible for WHEN and IF |
| Activation | Level |
| Tick | Required |
| Required context | Self |
| Required feature | `scene.outside_bounds` |
| Property | `margin`: literal Number, default `0`, finite and `>= 0` |

The predicate has no persistent state. `Once per activation` remains an
action-level execution policy: it fires on the false-to-true Level transition,
does not repeat while outside, rearms after re-entry, and may coexist with an
`Every occurrence` action that deliberately runs every tick while outside.

### Geometry

For `position = Self.Transform.position`, `size = activeScene.worldSize`, and
`margin >= 0`, the sole predicate formula is:

```text
outside = position.x < -margin
       || position.y < -margin
       || position.x > size.x + margin
       || position.y > size.y + margin
```

The exact perimeter is inside: equality at `-margin` or `size + margin` is
false. The formula deliberately uses the transform position only. It does not
model partially/completely outside visual bounds, individual sides, camera or
viewport bounds, or a global project-level observer.

### Runtime authority and failure path

Add the semantic query below to the shared gameplay host:

```cpp
virtual bool isOutsideSceneBounds(EntityId owner, float margin) const = 0;
```

`RuntimeLogicHostAdapter` resolves the entity transform and active scene world
size, validates all numeric inputs, and evaluates the formula above. Missing
entities/scenes, non-finite or non-positive world dimensions, and invalid
margins fail closed (`false`). No default position or world size is invented.

The Logic runtime exposes only `context.self:is_outside_scene(margin)`. The
generated condition calls that semantic API; it must not reproduce the geometry
in generated Lua or expose raw scene/query plumbing to the board.

### Authoring, persistence and Edit/Play

The descriptor uses the existing Logic block/property model. It adds no new
authoring component, workspace state, Intent, Command, asset, or project-schema
field; Logic Board mutations keep using their current atomic commands and
Undo/Redo/revision behaviour. No schema migration or Logic API-version bump is
needed. Existing feature negotiation rejects generated programs on runtimes
that do not support `scene.outside_bounds`.

RmlUi only projects the registry entry in WHEN and IF and collects the literal
margin. During Play, authoring stays frozen and the query reads only the
independent PlaySession runtime; runtime results never write back to the
document. The native UI requires no new listener or retained resource.

## Alternatives rejected

- **Global `On Any Entity Outside Scene` event:** changes ownership from an
  object-type board to project-wide logic and needs a separate global-authority
  design.
- **Raw position/world-size expressions in generated Lua:** duplicates geometry
  across generated programs and makes the runtime host cease to be the semantic
  authority.
- **Sprite/collider/tilemap bounds:** makes the result depend on optional
  presentation and collision components rather than a stable entity transform.
- **Automatic destruction:** removes valid recipes such as spawn, score, scene
  transition, audio, or IF composition; Destroy Self is already a normal THEN.
- **Dynamic margin expression:** adds validation/codegen complexity without a
  required first use case.

## Consequences

- `WHEN Outside Scene → Destroy Self` is expressible without scripts.
- The same Level condition is composable in IF and with all existing THEN
  actions, including Spawn Object.
- The host API expands by one semantic runtime query and all implementations,
  fakes, and export paths must implement it.
- Future variants (single-side, bounds-based, camera/viewport, fully outside,
  global observers) are distinct features and require their own decision.

## Verification

- Geometry: `(0,0)` and `(width,height)` at zero margin are inside; strictly
  less/greater x/y positions are outside; exact edges remain inside.
- Margin: `width + 16` is inside at margin `32`; `width + 33` is outside.
- Validation: negative, NaN, and infinite margins are rejected by the Logic
  validator; invalid host inputs fail closed.
- Codegen/feature negotiation: output calls `is_outside_scene`; required
  feature is recorded and unsupported hosts reject it explicitly.
- Execution: Once per activation fires once while outside, rearms after
  re-entry, and Every occurrence fires every outside tick.
- Parity: core Logic test, editor Logic Board projection test, Editor Play, and
  exported runtime exercise the same host query without authoring mutation.
