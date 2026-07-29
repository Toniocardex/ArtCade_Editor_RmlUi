# ADR-0045 — Scene Logic as a First-Class Runtime Scope

**Status:** Accepted  
**Date:** 2026-07-29  
**Scope:** persistent Scene Logic ownership, native Logic Board authoring,
compiler context policy, gameplay lifecycle, and Play/export parity.  
**Related:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`,
`ARTCADE_RMLUI_ARCHITECTURE.md`, `ARTCADE_RMLUI_ENGINEERING_GATES.md`
§4/§9/§18/§30, ADR-0039, ADR-0043, ADR-0044.

## Context

An Object Type Logic Board is instantiated once for every runtime entity. It
therefore has `Self`, represents local behaviour, and cannot correctly express
the lifecycle or global orchestration of a scene: level setup, waves, menu
flow, scene-local timers, input and project-state changes.

`SceneDef` already owns the scene composition — settings, layers and placed
instances — and is the aggregate root for scene-local persistent content. It
must also be the sole persistent authority for Scene Logic. A hidden controller
entity, a special Object Type, an elected first instance, a second map keyed by
scene, or `INVALID_ENTITY` masquerading as a scene would create a false owner
or duplicate authority.

## Decision

### Persistent authority and authoring

Add `std::optional<LogicBoardDef> SceneDef::logicBoard`. It is serialized with
the scene and follows the existing Logic Board schema/migration path. No
Object Type is created or altered when a Scene Logic Board is created.

Scene Logic mutations use scene-targeted Editor Commands through the existing
Intent/Coordinator boundary. They are atomic and undoable, change the document
revision/dirty state in the normal way, and are rejected during Play. The
Logic Board editor state carries an explicit owner target:

```text
Scene(sceneId) | ObjectType(objectTypeId)
```

The native board presents the selected target explicitly (persistent `SCENE`
or `OBJECT TYPE` badge) and allows opening the active scene board without
implicitly selecting an entity. RmlUi remains a projection and transient input
buffer only.

### Scene context capability policy

Introduce `LogicContextCapability::Scene`, used solely to express blocks whose
owner is the active scene rather than an entity. A Scene board has:

- Scene / Project / Runtime context;
- no `Self`, no entity owner, no fake entity identifier;
- access only to descriptors whose required context is satisfied by Scene.

For this MVP, scene-compatible blocks are the existing global/absolute ones:
`On Scene Start`, Every Frame/Seconds, keyboard input, project-state
conditions/actions, Wait, Spawn Object at an absolute position, Play Sound,
Camera Shake, Restart Scene and Go To Scene. Entity-local blocks and queries
remain unavailable: Destroy Self, transform/velocity/movement/animation
actions, object-local state, collisions, Outside Scene, and animation/entity
lifecycle events.

`Spawn Object` is compiled against a scene-scoped primitive and does not rely
on `Self`. Entity boards retain the same absolute spawn semantics; future
relative-to-Self spawn is a separate design.

### Runtime scopes and lifecycle

`LogicRuntime` exposes distinct explicit installation APIs:

```cpp
installScene(sceneId)
installEntity(objectTypeId, entityId)
```

The implementation may share subscription storage, but every scope records its
kind and owner identity. Scene callbacks receive a Scene context; entity
callbacks retain their current Self context. Scene dispatch APIs do not accept
an entity ID and entity dispatch APIs never target a scene scope.

On active-scene preparation, GameplaySession performs this deterministic
sequence:

```text
materialize scene composition
install Scene Logic scope
install existing Entity Logic scopes
dispatch On Scene Start
commit deferred spawns and install their Entity scopes
dispatch On Instance Start for those spawns
first input / fixed step
```

Spawns requested by On Scene Start are deferred through the existing gateway
queue. Their instance Start callbacks never re-enter the Scene Start callback.
On scene transition, restart, Play Stop, project replacement and shutdown, the
Scene Logic scope is cancelled before the corresponding local runtime teardown.
This MVP deliberately adds no `On Scene Exit` event.

### Generated program and runtime negotiation

`LogicProgram` gains an explicit target kind and target identifier, allowing
the compiler to produce both Object Type and Scene programs from their sole
persistent owners. Scene program source registers `context:on_scene_start` and
uses Scene-context bindings such as `context:spawn_object`.

Required runtime features remain explicit. Programs unsupported by the runtime
are rejected rather than falling back to a controller entity or a partial
execution. The runtime's active scene is authoritative for a Scene scope; the
authoring document is never read or mutated during dispatch.

### Edit/Play and persistence invariants

The authoring `ProjectDocument` remains immutable during Play. `PlaySession`
owns scene/entity subscriptions, callbacks, timers and queued spawns. Stop
cancels all scopes and disposes them with the session; no runtime state is
serialized or copied back to `SceneDef`.

Old projects with no `logicBoard` on a scene deserialize as having no Scene
Logic Board. The serializer must preserve a present empty board and round-trip
all Scene Logic blocks without inventing an Object Type.

## Alternatives rejected

- **Hidden `SceneController` entity:** changes the domain and gives global
  logic accidental transform, collision and Self capabilities.
- **Special Object Type / first instance as owner:** couples scene behaviour to
  composition and fails for empty scenes, menus and replacement.
- **`INVALID_ENTITY` as a scene owner:** makes scope identity ambiguous and
  encourages entity-only APIs to silently accept a non-entity.
- **A global scene-logic map separate from `SceneDef`:** duplicates persistent
  authority and complicates Save/Load/Undo.
- **Adding Scene Variables now:** no persistent scene-local data requirement
  exists in this slice; Project Variables and ephemeral scope state suffice.
- **On Scene Exit in the MVP:** transition/restart/Stop semantics and callback
  authority need a separate decision.

## Consequences

- Scene and Object Type Logic become visibly and semantically distinct.
- The Logic catalog must filter by context rather than infer an owner from UI
  selection.
- Serializer, commands, editor state, compiler, runtime, PlaySession and
  export template require coordinated but narrowly scoped changes.
- Existing Object Type Boards retain their behaviour and their serialized
  representation.

## Verification

- A Scene Logic Board can be created, edited, undone/redone, saved and loaded
  without creating an Object Type or instance.
- The native UI identifies Scene versus Object Type target and exposes only
  compatible WHEN/IF/THEN blocks.
- A Scene `On Scene Start` can set a project variable and spawn an entity; the
  spawned entity receives Instance Start only after the Scene callback returns.
- Entity-local blocks are rejected in a Scene board by core validation, not
  merely hidden by UI.
- Scene scope cancellation prevents timers/callbacks after transition, Stop
  and project replacement; no Play mutation changes authoring.
- Existing Object Type board compilation/execution, serializer fixtures and
  exported runtime remain green.
