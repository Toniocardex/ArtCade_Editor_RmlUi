# ADR-0055 — Platformer Contact Projection & Logic Board

**Status:** Accepted  
**Date:** 2026-07-31  
**Related:** ADR-0051, ADR-0052, ADR-0053, ADR-0054, ADR-0016  
**Scope:** Expose Floor / Wall-blocked / Ceiling as semantic contacts for Logic
Board; Pre/Post evaluation phases; Immediate vs NextSimulationStep effect
timing; wall jump/slide intents.  
**Out of scope:** Is Touching Wall probe, wall coyote, ledge grab, slopes,
Script post-simulation parity, Floor/Wall as `PlatformerState` values.

---

## Decision

### Model

Collider response, **Contact**, and **Locomotion** stay separate. Do not add
Floor/Wall/Landed/Ceiling to `PlatformerState`.

Locomotion modes that *are* `PlatformerState` (ADR-0016 / ADR-0052):

- **Climbing** — ladder modality (sticky while overlapping Interaction sensor)
- **WallSliding** — Wall Slide intent accepted for the step **and** current
  `xMove` blocked on the same side, with finite `maxFallSpeed >= 0`. Not a
  persistent wall probe. If Y lands in the same step, grounded Stopped/Moving
  wins over WallSliding.

Wall Jump remains an intent/impulse; resulting locomotion is **Jumping**.

### Projection

`World` publishes `PlatformerContactProjection` after each Platformer fixed
step (from `xMove` / `yMove` / final `findGroundSupport`). Logic reads it; no
second collision authority.

`landedThisStep = !wasGrounded && grounded && yMove.hitFloor`  
`landingImpactSpeed = max(0, preResolutionVy)`

### Phases vs effect timing

- `LogicPhaseRequirement`: PreOnly / PostOnly / Flexible (triggers & conditions)
- `LogicEffectTiming`: Immediate / NextSimulationStep (actions)

Rule phase is derived from trigger + conditions. Actions never choose the
phase. Post → Wall Jump/Slide queues a next-step intent.

### Channels

- Level observers: `onPostSimulation`
- Pulses: `onPlatformerLanded` / `onPlatformerWallContact` / `onPlatformerCeilingHit`

### Features

`logic.post_simulation_v1`, `platformer.contact_projection_v1`,
`platformer.wall_intents_v1`
