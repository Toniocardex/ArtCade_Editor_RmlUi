# ADR-0014 — BoxCollider2D collision authority and derived runtime body

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** shared runtime materialisation (`EntityDef` → session-local
`CollisionBody`), `RuntimeEntityGateway` / `World`, Editor Play /
`GameplaySession`, `game.exe`, WASM, project schema / migration, Inspector
and Scene View overlays  
**Related:**
[`COMPONENT_ALIGNMENT_AND_LEGACY_RETIREMENT_ROADMAP.md`](COMPONENT_ALIGNMENT_AND_LEGACY_RETIREMENT_ROADMAP.md)
(§3.3 Collision fork), [`NATIVE_EDITOR_MIGRATION.md`](NATIVE_EDITOR_MIGRATION.md)
(runtime collisions baseline), [`ADR-0010`](ADR-0010-sprite-animation-sheet-materialization.md)
(shared materialiser pattern), [`ADR-0021`](ADR-0021-topdown-boxcollider2d-collision.md)
(Top Down locomotion uses this CollisionWorld path), Constitution (single
authority; no runtime→authoring sync)

## Context

The native editor introduced `BoxCollider2DComponent` as the **simple,
object-type-owned** collision authoring model: one AABB, offset/size,
enabled flag, and mode (`Solid` / `Trigger` / `OneWayPlatform`). Edit-mode
Inspector, gizmo, and Scene View overlays read that field only. Play
originally materialised it into an editor-local `RuntimeBoxCollider` and
resolved kinematic-vs-static motion inside `PlaySession`.

RU-03 removed that editor-local simulator and moved Play onto the shared
`GameplaySession` / `World` / `RuntimeEntityGateway` stack. That stack’s
physics path consumes a richer session-local `CollisionBodyComponent`
(shapes, response, body type, layers/profiles). The shared spawn path
currently does:

```text
registry.setCollisionBody(id, def.collisionBody);
```

and ignores `def.boxCollider2D`. Authoring therefore writes one field while
Play/export read another. Observed symptom: entities with authored
BoxCollider2D pass through each other and never ground / fire collision
events. Tests that required floor collision were skipped for this gap.

`COMPONENT_ALIGNMENT` §3.3 already forbade treating `CollisionBody` as a
second editable authority beside `BoxCollider2D`, and required an ADR to
choose either full migration to `CollisionBody` authoring or a derived
materialisation with exactly one persistent source of truth. That ADR was
never closed; this document closes it.

## Decision (binding)

> `BoxCollider2DComponent` is the **only persistent collision authority** in
> ArtCade authoring. The runtime does **not** own a second authorable
> collision component: it **deterministically materialises** a session-local
> `CollisionBody` when creating a `GameplaySession` or spawning an entity.
> That representation is not persisted as authoring, not editable, not
> reverse-synced into `ProjectDocument`, and is destroyed with the session.

Authority flow:

```text
ProjectDocument
└── ObjectTypeDef
    └── BoxCollider2DComponent     ← sole persistent authority

Start Play / Spawn / Export / Restore
        ↓ pure conversion (one shared function)

Runtime CollisionBody              ← derived projection
        ↓
Physics World                      ← session state
```

A derived `CollisionBody` is **not** a second source of truth when all of
the following hold:

- it is not editable in the Inspector or via Commands;
- it is not serialised as a parallel authoring field in the current schema;
- it is rebuilt by the **same** shared materialiser on every host path;
- it is never written back to the document on Stop;
- it dies with the Play / game session.

### Mapping (authoring → runtime)

| `BoxCollider2D` | Runtime `CollisionBody` |
|---|---|
| `enabled == false` (or absent) | no body (`nullopt`) |
| `enabled == true` | one rectangle shape |
| `offset` | shape local offset |
| `size` | shape width / height (must be finite and positive) |
| `Solid` | solid response |
| `Trigger` | sensor / trigger response |
| `OneWayPlatform` | one-way solid profile |

**Body type** has exactly one policy for Slice 1+:

- Prefer a single shared query
  `resolveCollisionBodyType(const EntityDef&)` (for example: Top Down /
  Platformer / LinearMover → `Kinematic`, otherwise `Static`).
- That query must not be reimplemented in gateway, World, Play, and export.
- If product later requires an explicit override, it is added **on
  `BoxCollider2DComponent`**, not as a separate authoring `CollisionBody`.

Materialisation is deterministic: same Object Type `BoxCollider2D` (+ same
body-type inputs) → same runtime body.

### Single shared materialiser

Conversion must **not** live only in the editor’s `PlaySession` facade.
A pure shared module on the runtime path (same idea as ADR-0010’s
`object-type-materialize`) owns it, for example:

```text
materializeBoxCollider2D(const EntityDef&)
  → optional CollisionBodyComponent
```

or, preferably, as part of the sole entity materialiser:

```text
materializeRuntimeEntity(project, objectType, instance)
  → RuntimeEntityDefinition   // already carries derived collision body
```

Every creation path must use that function:

- initial scene load
- dynamic spawn (`spawnFromClass` / Logic / Script)
- scene restart
- save-load restore into a live session
- Editor Play
- `game.exe`
- WASM
- headless tests

There must be **no** host-specific converter and **no** permanent fallback
of the form “use `collisionBody` if present, else convert `boxCollider2D`”.

Conceptually wrong:

```text
gateway.setCollisionBody(id, objectType.collisionBody);
```

Required shape:

```text
body = materializeBoxCollider2D(objectType);
gateway.setCollisionBody(id, body);   // or spawn(alreadyMaterializedDef)
```

Ideal end state: the gateway receives a **runtime definition already
materialised**, so a new spawn path cannot forget the conversion.

### Persistent schema

| Field | Role after this ADR |
|---|---|
| `EntityDef.boxCollider2D` | written and read as current authoring |
| `EntityDef.collisionBody` | **not** current authoring; legacy read/migration only |

`CollisionBodyComponent` remains a valid **runtime / session** type. It must
not be an Inspector Add Component target and must not have authoring
Commands.

#### Migration (legacy projects)

| On load | Action |
|---|---|
| only `boxCollider2D` | use it |
| only legacy `collisionBody` | migrate to `BoxCollider2D` when representable as one enabled AABB + mode |
| both present | **no silent pick** — explicit diagnostic / assisted migration |
| legacy circle / polygon / multi-shape | **no silent crush** into a box — actionable migration error or assisted conversion |

After migration:

- in-memory authoring model carries only `boxCollider2D`;
- writer emits only `boxCollider2D` (no dual-write);
- readers may still understand legacy `collisionBody` for migration.

### Inspector and Scene View

Continue to read **only** `EntityDef::boxCollider2D` for:

- Inspector fields
- gizmo / overlay / bounds
- Solid / Trigger / OneWay UI
- validation
- Undo / Redo

Do **not** add “Add Component → Collision Body” or expose runtime body
properties as authoring. Play-time physics debug may show the materialised
body as an explicit **read-only projection** (“derived from BoxCollider2D”).

Authoring validators stay on `BoxCollider2D` (finite positive size, finite
offset, valid mode, object-type-only, no unexpected instance override). They
must not try to keep two persistent configurations aligned.

## Authority and module boundaries

| Concern | Owner |
|---|---|
| Persistent collision | `ProjectDocument` / `ObjectTypeDef.boxCollider2D` |
| Derived body at create/spawn | Shared runtime materialiser |
| Physics resolution / events | `World` / collision stack (session only) |
| Edit overlay | Editor projection from `boxCollider2D` |
| UI / RmlUi | Presentation + Commands on `boxCollider2D` only |

Forbidden (stop-the-line):

- `BoxCollider2D` and `CollisionBody` both editable;
- two Inspector sections or two Command families for collision;
- conversion implemented only in Editor Play;
- dynamic spawn using a different conversion path;
- `World` choosing between two **persistent** definitions each tick;
- bidirectional sync BoxCollider ↔ CollisionBody;
- permanent dual fallback as the “solution”;
- migration that silently drops non-box legacy shapes;
- Physics World reading `ProjectDocument` per frame.

## Alternatives rejected

1. **Author `CollisionBody` and retire `BoxCollider2D` immediately.**
   Rejected for this product stage: the editor already ships BoxCollider2D
   as the simple authoring surface; a full shape/profile Inspector is out of
   scope and would reopen dual authority during transition.
2. **Make `World` consume `BoxCollider2D` directly.** Rejected: couples the
   physics stack to the authoring schema and blocks richer runtime shapes
   later without a clean derived layer.
3. **Editor-only Play bridge.** Rejected: leaves `game.exe` / WASM /
   spawn-from-class divergent (same class of failure ADR-0010 rejected).
4. **Permanent “prefer collisionBody else boxCollider2D”.** Rejected:
   freezes the fork instead of closing it.
5. **Silent dual-write of both fields.** Rejected: two persistable truths.

## Implementation slices

### Slice 1 — Runtime restore (P0)

- Introduce the pure shared materialiser BoxCollider2D → CollisionBody.
- Wire it into the **sole** shared entity creation path
  (`applyEntityDefToRegistry` / materialise-then-spawn).
- Cover initial scene load and dynamic spawn.
- Stop using authored `def.collisionBody` on that path.
- Verify Editor Play, native, and WASM.

### Slice 2 — Schema closure

- Freeze body-type policy (`resolveCollisionBodyType` and/or explicit field
  on `BoxCollider2D`).
- Remove `collisionBody` from the **current** project form (readers ignore
  the entity/objectType key; writers already emit only `boxCollider2D`).
- Drop transitional dual fallbacks on the spawn path — session body comes
  only from `materializeBoxCollider2D`.
- Explicit assisted migration for non-box legacy shapes remains optional
  technical debt (no silent crush of circle / multi-shape / layers).

### Slice 3 — Dead code removal

- Remove authoring Commands / resolvers / UI for `collisionBody` if any
  appear; grep-gate authoring access.
- Update roadmaps to cite this ADR as the closed decision.

### Slice 4 — Parity and tests

Minimum proofs:

- Solid → runtime solid rectangle
- Trigger → runtime sensor
- OneWay → runtime one-way
- disabled → no body
- remove BoxCollider → new session has no body
- scene load = dynamic spawn = restart = native export = WASM
- change offset/size → Save/Reload → Play → same runtime collider
- current project JSON has no authoring `collisionBody`

Re-enable editor Play observations previously skipped for the
boxCollider→collisionBody gap (platformer grounded / collision enter-exit)
once Slice 1 lands.

## Consequences

- One authoring truth; physics stays a derived session projection.
- RU-03’s single simulator is preserved without reintroducing an editor-only
  collision stack.
- `COMPONENT_ALIGNMENT` §3.3 option 2 is the accepted path; option 1
  (full CollisionBody authoring) remains a **future** ADR if product needs
  multi-shape / profile editing — it must migrate **from** BoxCollider2D,
  not coexist with it.
