# ADR-0051 — Logic Pre-/Post-Simulation Evaluation Phases

**Status:** Accepted  
**Date:** 2026-07-30  
**Scope:** Fixed-step ordering of Logic Board tick rules relative to
`PlatformerController` / simple-movement integration; descriptor metadata for
evaluation phase; generated Lua install channels; Play/`game.exe` parity for
Platformer State → Play Clip.  
**Related:** [`ADR-0016-platformer-state.md`](ADR-0016-platformer-state.md),
[`ADR-0015-platformer-motion-state.md`](ADR-0015-platformer-motion-state.md),
[`ADR-0012-platformer-move-direction.md`](ADR-0012-platformer-move-direction.md),
[`ADR-0043-logic-outside-scene-bounds.md`](ADR-0043-logic-outside-scene-bounds.md),
[`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md) §2B.1,
`ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` §4–§6,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§11–§14.

## Context

ADR-0016 made `World::platformerState` the sole authority for Stopped /
Moving / Jumping / Falling. The canonical authoring recipe is:

```text
While Key Held → Move Horizontal / Flip   (intent producers)
Platformer State · OncePerActivation → Play Clip   (state observers)
```

Input must **not** become animation authority. Clips follow the controller’s
final locomotor state for the tick.

Today `GameplaySession` (composition root shared by editor Play and export)
orders work roughly as:

```text
1. logicRuntime_->dispatchTick(dt)     // all on_update / Key Held / Level guards
2. scriptRuntime_->update(dt)          // manual on_update (intent override)
3. world_->tickPlatformerControllers(dt)
4. world_->tickSimpleMovementIntents(dt)
5. … physics / collision / camera …
6. render
```

`platformerState()` projects `PlatformerRt` **after** integration. Rules that
observe that projection therefore see the **previous** tick’s state when they
run inside the single `dispatchTick` above. Consequence on a one-tick hold:

```text
tick N:   D held → intent set → state still Stopped → Idle / no Walk yet
          → controller moves → state becomes Moving (after Logic)
          → render: entity already moved, Walk not selected yet

tick N+1: Walk finally selected (one tick late)
release:  Walk may linger one tick after velocity is zero
```

Two wrong “fixes” that must not ship:

1. **Artificially prolong key hold / fake input duration** so Walk appears —
   couples animation to input, fights ADR-0016.
2. **Wire Walk directly to Key Held** — input becomes clip authority; jumps,
   launches, and Script movement desync from anim.
3. **Move the entire `dispatchTick` after the PlatformerController** —
   `Every Tick` / `While Key Held` → `Move Horizontal` would run **after**
   integration; movement itself would slip to the next tick.

The robust split is two Logic phases in the same fixed step: rules that
**produce intents** before simulation; rules that **observe resulting state**
after simulation; then render.

A second, orthogonal issue: even with correct ordering, a movement that lasts
only one fixed step shows Walk for only one rendered frame. An 8 FPS walk
cannot advance past frame 0 if the entity has already stopped. That is an
**animator policy** problem (minimum state duration / exit time), not a
PlatformerController or generic Logic Board problem — out of scope here.

## Decision

### Two evaluation phases (not a full scheduler)

Introduce an explicit phase on the trigger descriptor:

```cpp
enum class LogicEvaluationPhase {
    PreSimulation,   // default — intent / input / every-tick producers
    PostSimulation,  // observers of post-integration world state
};
```

Owned by `LogicBlockDescriptor` (registry = sole authority), next to
`activationKind`. **Not** inferred from display names or typeId string matching
in the gameplay driver.

Platformer State becomes:

```text
activationKind:   Level
evaluationPhase:  PostSimulation
```

Persistent project JSON stays a **single** rule definition. Generated Lua
installs the same rule body into the correct tick **channel** for its trigger
phase.

### Fixed-step sequence (binding)

```text
1. sample input
2. logicRuntime_->dispatchPreSimulationTick(dt)
      // Every Tick, Key Held, movement / jump request actions, …
3. scriptRuntime_->update(dt)   // remains pre-integration (intent override)
4. world_->tickPlatformerControllers(dt)
5. world_->tickSimpleMovementIntents(dt)
6. // PlatformerRt / platformerState authoritative for this tick
7. logicRuntime_->dispatchPostSimulationTick(dt)
      // Platformer State, Is Grounded, Outside Scene, future Landed / Left Ground
8. … remaining post-sim work (physics sync, collisions, …) as today unless a
   future ADR moves a specific observer
9. render
```

Same tick, first movement:

```text
D held → pre: Move Horizontal
       → controller integrates → state Moving
       → post: Platformer State Moving → Play Walk
       → render Walk
```

Release:

```text
no intent → controller stops → state Stopped
         → post: Play Idle
```

`dispatchTick(dt)` may remain as a thin compatibility wrapper that calls both
phases in order **only** for unit hosts that do not own a World; production
`GameplaySession` must call the two entry points around controller integration
and must not double-dispatch.

### Initial classification

| Phase | Triggers (initial) |
|---|---|
| **PreSimulation** (default) | Every Tick / `event.on_update`, While Key Held, Key Pressed (and other pulse input), movement actions’ parent rules that set intents, Jump request rules |
| **PostSimulation** | Platformer State (`platformer.motion_state`), Is Grounded (`platformer.is_grounded`), Outside Scene (`scene.outside_bounds`), future Landed / Left Ground / Started Falling when shipped |

Rules are classified by the **trigger** descriptor’s `evaluationPhase`. Actions
do not carry a separate phase. Nested IF conditions under a pre-sim trigger
still run in pre-sim (authors must not put Platformer State as a **condition**
under Key Held expecting post-sim freshness — Prefer Platformer State as WHEN
trigger per ADR-0016).

### Explicit non-goals / rejected approaches

- Do **not** move all Logic after the controller.
- Do **not** hard-code a Walk delay or hold-extension in PlatformerController
  or Logic Runtime (“Hero stopped, Walk still playing” is wrong for v1).
- Do **not** drive Walk/Idle from Key Held.
- Do **not** invent a general-purpose multi-phase scheduler, job graph, or
  priority queue in this slice — two named channels are enough.
- Do **not** add minimum clip / state duration here — that belongs to a future
  Animation State Machine ADR.

### Script and non-tick events

- Manual Script `on_update` stays **between** pre-sim Logic and platformer
  integration (existing override contract).
- Pulse / lifecycle / collision / animation-finished dispatches are **unchanged**
  by this ADR; they are not forced into PostSimulation merely because they are
  “observational.”
- `Every N Seconds` / delayed callbacks: remain on the pre-sim tick path unless
  a later ADR reclassifies a specific timer that must observe post-sim state.

### Authoring guidance (immediate, no code change required)

Until / while the phase split lands:

- Prefer **Play Clip · OncePerActivation** on Platformer State (already ADR-0016).
- Use a Walk first frame visually distinct from Idle.
- Keep Walk around **8–12 FPS** so a short hold is readable when it lasts more
  than one frame.
- Do not treat asset preload as the fix for one-tick lag — ordering is the bug.

### Persistence and editor

- No new `ProjectDocument` field; no Command/Intent for phase.
- Phase is catalog metadata only (`logic-core` registry).
- Logic Board UI may later show a read-only “runs after simulation” hint for
  PostSimulation triggers; not required for the runtime slice.
- Save/load of boards unchanged; only codegen install channel changes.

## Implementation (slice)

Minimal runtime slice (no generic scheduler). **Delivered with ADR-0055**
(`LogicPhaseRequirement` + `LogicEffectTiming`; rule phase from trigger+conditions):

1. [x] Phase model on descriptors; PostOnly on Platformer State, Is Grounded,
   Is Blocked By Wall, Outside Scene; PreOnly on input producers.
2. [x] Codegen installs Pre → `on_update`, Post → `on_post_simulation`;
   pulse contacts → dedicated channels (ADR-0055).
3. [x] `dispatchPreSimulationTick` / `dispatchPostSimulationTick`.
4. [x] `GameplaySession` fixed-step: Pre → script → simulate → collision +
   platformer pulses → Post → flush destroy.
5. [x] `scriptRuntime_->update` stays pre-integration.
6. [ ] Fundamental two-fixed-step Walk-clip same-tick coverage (animator policy
   tests; see Verification below).

Roadmap §2B.1 “Ordine di dispatch” that still documents Logic before physics
with while-on-previous-state is **superseded for Level observers of
`platformerState` / grounded / outside-scene** by this ADR. Edge latch rules
(Landed / Left Ground) remain post-sim when implemented.

## Consequences

- Same-tick consistency: intent → integrate → observe → animate → render.
- ADR-0016 recipe works without making input the animation authority.
- Pre-sim movement rules keep zero-latency intent relative to integration.
- Unit tests that call a single `dispatchTick` without a World must either
  invoke both phases or use the compatibility wrapper deliberately.
- One-frame Walk on a one-tick tap remains possible; readability of taps is a
  future animator policy, not a reason to fake hold duration now.

## Verification

Fundamental regression (fixed `dt`, one entity with PlatformerController +
Sprite Animator + board recipe):

```text
tick 1: input Right
  → position.x increases
  → post-simulation platformerState == Moving
  → current clip == walk   // before render of this tick

tick 2: input absent
  → post-simulation platformerState == Stopped
  → current clip == idle
```

Also:

- Hold across many ticks: Walk selected once (OncePerActivation); no restart
  while Moving remains true.
- Jump request pre-sim → Jumping observed post-sim same tick (or next air
  sub-step per existing controller semantics — assert no Idle flash at apex per
  ADR-0016).
- Moving `dispatchTick` wholesale after the controller must **not** be the
  implementation (movement latency test: Key Held + Move Horizontal still
  applies displacement in the same tick as the key sample).
- Editor Play and exported runtime share `GameplaySession` ordering.

## Out of scope

- Animation State Machine, minimum state duration, exit-time transitions.
- Hard-coded Walk hold/delay.
- Rework of collision / On Start / anim-finished dispatch order.
- New authoring Commands or ProjectDocument schema for phase.
- Full rewrite of `LOGIC_BOARD_RULES_ROADMAP` edge latch design (only the
  while-observer timing for post-sim state is corrected here).
