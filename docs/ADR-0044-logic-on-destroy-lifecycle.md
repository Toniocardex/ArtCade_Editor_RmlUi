# ADR-0044 — Logic Board On Destroy Lifecycle Trigger

**Status:** Accepted
**Date:** 2026-07-29
**Scope:** Logic Board trigger `lifecycle.on_destroy`, gameplay destruction
pipeline, native Logic Board catalog, Editor Play and export parity.
**Related:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§9/§18/§30, ADR-0026, ADR-0043.

## Context

`Destroy Self` is a deferred request. Once the entity is erased, its Logic
scope and all of its subscriptions must be gone; consequently `Is Destroyed`
cannot be a condition evaluated by Self. Authors instead need one central
place for death effects shared by multiple destruction causes:

```text
WHEN Outside Scene       THEN Destroy Self
WHEN On Collision Enter  THEN Destroy Self
WHEN On Destroy          THEN Spawn Object, Play Sound, Add Score
```

The runtime gateway already runs its destroy hook while the entity still
exists. The existing generic script lifecycle notification is post-erase and
therefore cannot be used by a Self Logic Board callback. `World` is the
authority for accepted gameplay destruction requests and for the transition
from a live entity to runtime teardown.

## Decision

### Block contract

Add the following Logic Board trigger:

| Item | Value |
|---|---|
| typeId | `lifecycle.on_destroy` |
| Display name | On Destroy |
| Category | Lifecycle |
| Kind | Trigger, WHEN only |
| Activation | Pulse |
| Tick | Not required |
| Context | Self |
| Required feature | `lifecycle.on_destroy` |
| Properties | None |

It is named **On Destroy**, not “On Destroyed”: its actions run immediately
before teardown, while Self's transform, Object Type, local variables and
normal gameplay host operations remain valid. A Pulse occurs at most once per
runtime entity lifetime, so action execution modes retain their existing
meaning without requiring a separate trigger latch.

### Authoritative destruction flow

Only explicit gameplay destruction emits this MVP trigger:

```text
requestDestroy(entity)
  → mark PendingGameplayDestroy (idempotent)
  → gateway queue snapshot
  → pre-teardown On Destroy dispatch once
  → World runtime cleanup and scope cancellation
  → physics and registry/entity teardown
```

`World::requestDestroy` owns the transient pending/destroying identity sets.
Repeated requests for a pending or currently-dispatching entity return success
without queueing another destroy. The pre-teardown hook is invoked only for an
ID marked through this API; it is called before `World` clears local variables,
controller state, animation state, or the session cancels Logic/Script scopes.
The sets are cleared on completion and when World runtime state is reset, so a
recycled entity ID never inherits a pending destruction marker.

The existing gateway batch/snapshot queue remains the sole mutation mechanism:
new destroys requested by a callback are processed in a later batch, and an
entity spawned by On Destroy is not part of the current destroy batch.

### Causes and exclusions

`Destroy Self`, `Destroy Other`, the manual script context destroy API, and
`entity.destroy` / `object.destroy` route through `World::requestDestroy` and
emit On Destroy. Script bindings return the request result rather than bypass
the World through `queueDestroy`.

Auto-destroy expiry, scene restart/transition, Editor Play stop, project
replacement, session shutdown, spawn-install rollback, and other technical
gateway teardown do **not** emit On Destroy in this slice. A future
`DestroyCause` model may make additional causes explicit; it is intentionally
not introduced here.

### Runtime, failure and teardown

`LogicRuntime` gains `EventKind::Destroy`, a `ContextProxy::onDestroy` binding
and public `dispatchDestroy(owner)`. Code generation registers
`context:on_destroy(ruleId, callback)`. The GameplaySession's pre-teardown
World callback dispatches this event before it cancels the owner scope.

Lua action errors are captured by the existing protected-callback diagnostics
path. They deactivate the failing subscription according to current runtime
policy but never cancel the gateway destroy operation: after dispatch returns,
World cleanup and entity teardown always proceed. Calling `Destroy Self` from
On Destroy is a successful idempotent request and cannot recursively dispatch
another On Destroy event.

`lifecycle.on_destroy` is added to the Logic runtime feature set. A program
requiring it is rejected explicitly by an older runtime; no fallback or
post-destroy polling is generated.

### Authoring, persistence and Edit/Play

The trigger uses the existing registry, rule commands, Undo/Redo, serializer
and catalog projection. No `ProjectDocument` schema, component, workspace
state, Intent or Command is added. RmlUi derives the Lifecycle category from
the registry and remains only a projection.

In Edit mode the board is ordinary persistent authoring data. In Play mode the
transient pending/destroying sets, subscriptions and callbacks belong only to
the independent PlaySession. No lifecycle result writes back to authoring.

## Alternatives rejected

- **`Is Destroyed` condition on Self:** Self has no scope after removal and
  cannot evaluate it.
- **Reuse post-erase script lifecycle events:** transform, variables and Logic
  scope have already been torn down.
- **Global “Entity Destroyed” observer:** needs a persistent target model or a
  destruction snapshot and is a separate feature.
- **Emit for every gateway destroy:** scene/session technical teardown could
  trigger unwanted effects, score changes and spawns.
- **Inline Spawn/Sound before every Destroy Self:** works for one cause but
  duplicates death effects across independent causes.

## Verification

- Registry/codegen/runtime feature: On Destroy is a Pulse WHEN trigger and
  generated Lua calls `on_destroy` with the required feature.
- Idempotence: three explicit requests produce one callback and one teardown;
  `Destroy Self` inside On Destroy does not recurse.
- Pre-teardown authority: callback can read Self position, update state and
  spawn; the source entity is removed afterwards and the spawned entity stays.
- Failure path: an action error emits a diagnostic while the entity and its
  Logic/Script scope are still torn down.
- Cause policy: explicit Logic and script destroys emit; AutoDestroy and
  scene/session teardown do not.
- Parity: native core tests, Logic Board editor tests, Editor Play and exported
  Windows runtime all use the same World destruction authority.
