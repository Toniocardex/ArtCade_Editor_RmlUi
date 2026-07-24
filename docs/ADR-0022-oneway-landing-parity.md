# ADR-0022 — One Way Platform resting parity (single support path)

**Status:** Accepted  
**Date:** 2026-07-24  
**Scope:** shared runtime kinematic resolve / Platformer ground support;
`BoxCollider2D` Solid vs One Way resting contact  
**Related:** [ADR-0014](ADR-0014-boxcollider2d-collision-authority.md),
Platformer controller in `World`

## Context

`BoxCollider2D` Solid and One Way materialise the same rectangular Solid shape;
One Way differs only by `shape.oneWay = true`. Sweep/separation already treat
One Way as eligible when descending from above (`platform.minY + 2`).

`CollisionWorld::isGrounded` used a **4 px** probe under the feet, and the
Platformer “floor snap” block only zeroed `vy` after a separate grounded check
— without correcting `Transform`. One Way landings could therefore rest with a
visible gap while Solid looked flush.

## Decision

> **Solid and One Way share the same resting contact:**
> `feet.maxY == support.minY`. One Way differs **only** in directional
> eligibility (descend/rest from above). A single `findGroundSupport` query
> produces both grounded and `correctionY`; never `isGrounded` then a second
> floor search.

| Mode | Eligibility | Resting |
|---|---|---|
| Solid | any direction | `feet.maxY == support.minY` |
| One Way | `requestedVerticalVelocity >= 0` and came from above | same |
| Sensor | never a physical support | — |

Named constants:

- `kGroundContactSkin` (1) — near-flush contact recognition / correction  
- `kOneWayApproachTolerance` (2) — “came from above” only (not resting offset)  
- `kGroundHorizontalOverlapEpsilon` (0.01) — require horizontal overlap

`resolveKinematicCollisionBody` returns `KinematicCollisionResult` and applies
snap before the caller writes Transform/Physics. Platformer consumes that
result; it does not rebuild + re-query grounded for floor snap.

`collisionGrounded(id, verticalVelocity)` wraps the same support path
(velocity-aware for One Way ascending). The zero-arg overload remains for
compatibility.

## Invariants

- Resting: `feet.maxY == support.minY` (float tolerance in tests)  
- One Way from below: no collision / no snap  
- One Way ascending: not grounded  
- No support: no Transform change from support path  
- Physics receives already-resolved Transform  

## Consequences

- Ground skin ≤ 1 px; former `+2` kept as approach tolerance only  
- No project schema, authoring, or migration changes  
- Top Down / Linear Mover not switched onto this resolver in this slice  
