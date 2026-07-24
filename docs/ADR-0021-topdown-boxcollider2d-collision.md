# ADR-0021 — Top Down locomotion uses BoxCollider2D / CollisionWorld

**Status:** Accepted  
**Date:** 2026-07-24  
**Revised:** 2026-07-24 (Slice 2 debt removal)  
**Scope:** shared runtime `World` Top Down step; relationship to
`BoxCollider2D` materialisation (ADR-0014), Platformer kinematic resolve,
and the session Physics module  
**Related:** [ADR-0014](ADR-0014-boxcollider2d-collision-authority.md),
[ADR-0005](ADR-0005-top-down-logic-input.md) (input intents only),
[Engineering Gates](ARTCADE_RMLUI_ENGINEERING_GATES.md) § authority / Play

## Context

`BoxCollider2D` is the sole persistent collision authority (ADR-0014). It
materialises to a session `CollisionBody` consumed by `CollisionWorld`.
Platformer locomotion already integrates `Transform` and calls
`World::resolveKinematicCollisionBody` against that world, then pushes
position/velocity into any Physics handle and is **excluded** from
`syncPhysicsToEntities` (Transform owns the body).

Top Down historically took a different path: accelerate an internal
velocity, then — when a Physics handle exists — `setLinearVelocity` and
**return**, never sweeping `CollisionWorld`. Walls authored only as
`BoxCollider2D` Solid therefore do not block Top Down characters.
Observed product symptom: “Top Down does not collide with Box Collider 2D.”

There is no Box2D library in this stack; “Box 2D collider” means the
authoring component `BoxCollider2D`.

## Decision (binding)

> **Top Down solid blocking uses the same collision authority and resolve
> path as Platformer:** authored `BoxCollider2D` → derived session
> `CollisionBody` → `CollisionWorld` → `resolveKinematicCollisionBody`.
> The Physics module must **not** be a second authority for Top Down
> locomotion vs solids.

### Authority

| Concern | Owner |
|---|---|
| Persistent collision | `ObjectTypeDef.boxCollider2D` (ADR-0014) |
| Session solids / sensors / one-way | `CollisionWorld` (derived) |
| Top Down speed / accel / friction / four-dir | `TopDownControllerComponent` |
| Top Down move intent | Logic `topdown.move` → `World` intent (ADR-0005) |
| Resolved pose each fixed step | `Transform` (Top Down owns it, like Platformer) |

### Runtime contract (this slice)

Per fixed step, for each active Top Down entity (and no Platformer on the
same entity):

```text
intent → TopDown velocity (accel / friction)
      → integrate Transform by velocity * dt
      → resolveKinematicCollisionBody vs CollisionWorld
      → write Transform (+ clipped velocity)
      → if Physics handle: push position + velocity (never pull)
```

`World::syncPhysicsToEntities` **skips** entities with
`TopDownController`, exactly as it already skips `PlatformerController`.

Solid / Trigger / OneWay behaviour for Top Down vs authored colliders is
whatever `CollisionWorld` + `resolveKinematicCollisionBody` already
implement for Platformer. No Top-Down-specific collision semantics.

### Forbidden

- Treating Physics body–body contacts as the way Top Down “sees”
  `BoxCollider2D` walls.
- Reintroducing an editor-only AABB mover for Top Down (RU-03).
- A permanent dual fallback: “Physics if handle else CollisionWorld.”
- Authoring a second collider size for locomotion beside `BoxCollider2D`.

## Former debt (removed — Slice 2)

| Item | Resolution |
|---|---|
| `ensurePhysicsBody` special-cased `hasTopDown` | Removed: Physics body only when explicit Physics collider size is authored |
| Top Down Physics `Dynamic` vs CollisionBody `Kinematic` | `resolvePhysicsBodyRules`: Top Down + explicit collider → `Kinematic` (same as Platformer) |
| Dual default 32×32 Physics geometry for Top Down | Gone with the `hasTopDown` ensure path; locomotion size is `BoxCollider2D` only |
| Missing Solid / Trigger / disabled proofs | `world_topdown_boxcollider_test` (no Physics handles on hero or wall) |

Optional Physics handle on a Top Down entity is allowed only for an
**explicit** authored Physics collider; it is push-aligned from Transform and
never the solid-blocking authority.

## Alternatives rejected

1. **Sync all `BoxCollider2D` solids into the Physics module and keep
   velocity-driven Dynamic Top Down.** Rejected: freezes a second collision
   world, duplicates ADR-0014, and diverges from Platformer.
2. **Editor Play special case only.** Rejected: same class of failure
   ADR-0010 / ADR-0014 rejected (`game.exe` / WASM / spawn diverge).
3. **Leave Top Down on Physics and document “add Physics collider to
   walls.”** Rejected: contradicts BoxCollider2D as sole authoring
   authority.

## Implementation slices

### Slice 1 — Authority restore — **done**

- `stepTopDownController`: integrate → resolve → setTransform → optional push.
- Top Down excluded from `syncPhysicsToEntities`.

### Slice 2 — Debt removal — **done**

- `ensurePhysicsBody` no longer creates bodies for Top Down alone.
- Top Down + explicit Physics collider → `Kinematic` (gravityScale 0).
- Tests prove Solid blocks with **zero** Physics handles on hero and wall.

### Slice 3 — Parity — **done** (headless)

- Solid / Trigger / disabled covered by `world_topdown_boxcollider_test`.
- Editor Play and `game.exe` share the same `World` / `GameplaySession` path.

## Consequences

- One collision story for characters: BoxCollider2D → CollisionWorld.
- Top Down and Platformer share resolve; input remains Logic-only
  (ADR-0005).
- Physics may still exist for other gameplay, but not as Top Down’s
  wall authority.
