# ADR-0015 — Platformer Motion (Moving / Stopped)

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** Logic Board condition `platformer.motion_state`, shared runtime query
`World::isPlatformerMovingHorizontally`, `IGameplayRuntimeHost` /
`GameplaySession` / Logic codegen / Script SelfProxy parity  
**Related:**
[`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md) §2B.1,
[ADR-0012](ADR-0012-platformer-move-direction.md) (Move Horizontal Left/Right),
[ADR-0005](ADR-0005-top-down-logic-input.md), Constitution (single authority;
no runtime→authoring sync)

## Context

Authors commonly write:

```text
While Key Held D → Move Horizontal Right + Play Walk + Flip Right
```

On key release the Move intent correctly stops (frame-local), but **Play Clip
is never superseded**. The last clip remains Walk. That is not an animator
bug: no Logic rule transitions to Idle.

Workarounds fail for real cases:

| Approach | Failure |
|---|---|
| `Key Released D → Play Idle` | Breaks while A is still held; ignores accel/decel, Script/AI motion, gamepad, external forces |
| Bare `Is Moving` boolean pair | Ambiguous vs Falling / Airborne; two typeIds for one negation |
| Persist `isMoving` / `isStopped` on `PlatformerControllerComponent` | Second authority beside runtime `PlatformerRt.velocity` |
| Auto clips on the Platformer component | Couples gameplay controller to sprite presentation |

Roadmap §2B.1 already forbade an unqualified `Is Moving` typeId and planned a
horizontal-motion Level predicate. This ADR freezes the shipped shape.

## Decision

### One Level condition, two authored states

Stable typeId: **`platformer.motion_state`** (`Logic::kPlatformerMotionState`).

UI label: **Platformer Motion**. Property **`state`**: closed choice
**`Moving` | `Stopped`** (default `Moving`). Synonyms include moving, stopped,
idle, walking, running, velocity, motion.

- Kind: Condition, Level (`LogicTriggerActivationKind::Level`) → eligible as
  WHEN via existing `on_update` + guard; OncePerActivation rising-edge applies
  unchanged.
- Requires `PlatformerController` + Self (same capability pattern as
  `is_grounded` / `is_falling`).
- Feature id: `platformer.motion_state`.

**Not** two blocks `Is Moving` / `Is Stopped`. **Not** two persistent booleans
on authoring or on the Logic Board.

### Single runtime query (velocity authority)

```text
PlatformerRt.velocity.x
        ↓
World::isPlatformerMovingHorizontally(id)
        ↓
IGameplayRuntimeHost::isPlatformerMoving(owner)
        ↓
context.self:is_platformer_moving()   (Logic / Script)
```

```cpp
// policy constant — not an authoring property
constexpr float kPlatformerMotionEpsilon = 0.01f;

bool World::isPlatformerMovingHorizontally(EntityId id) const {
    // requires PlatformerController; reads |PlatformerRt.velocity.x| > ε
}
```

| Authored state | Generated guard |
|---|---|
| `Moving` | `context.self:is_platformer_moving() == true` |
| `Stopped` | `context.self:is_platformer_moving() == false` |

Do **not** emit raw `velocity.x` comparisons in board Lua (duplicates ε,
leaks internals, diverges across hosts).

**Stopped ≠ Idle.** Idle is composition:

```text
Walk  = Platformer Motion Moving  AND Is Grounded
Idle  = Platformer Motion Stopped AND Is Grounded
```

Without Is Grounded, Stopped while airborne would overwrite Jump/Fall clips.

### Intent vs actual motion

| Signal | Meaning | This ADR |
|---|---|---|
| Has Horizontal Input (future) | Controller *will* | Out of scope |
| **Platformer Motion** | Controller *is* moving horizontally | **This** — `|vx| > ε` |

Intent-based “moving” is a separate future block if needed for input-led
recipes. Walk/Idle must track **effective** horizontal velocity (incl.
deceleration after key release).

### Timing

`LogicRuntime::dispatchTick` runs **before** platformer/physics integration in
the fixed step. The query therefore reads the **last completed** fixed step
(`PlatformerRt` after prior integration). Max latency ≈ one fixed step
(~16.7 ms at 60 Hz). Do **not** reorder the global tick or add a second
post-physics Logic dispatcher for this predicate alone.

### Authoring recipe (canonical)

```text
R1  While Key Held D → Move Horizontal Right, Flip Right
R2  While Key Held A → Move Horizontal Left, Flip Left
R3  Platformer Motion Moving  + IF Is Grounded
      · Once per activation → Play Clip Walk
R4  Platformer Motion Stopped + IF Is Grounded
      · Once per activation → Play Clip Idle
R5  Key Pressed Space → Jump, Play Clip Jump
```

Input rules own movement/facing. Motion-state rules own clip selection. Do
not put Play Walk inside both A and D rules.

**Every occurrence** + Play Clip on a Level motion WHEN is incorrect if Play
Clip restarts the clip each tick. Prefer **Once per activation** for
Stopped↔Moving edges.

### Collapsed rule summary

Summary must include the `state` value, e.g. `Platformer Moving → Play Clip`
/ `Platformer Stopped → Play Clip`, not a bare `Platformer Motion → …`.

## Consequences

- Logic Board stays a **read-only projection** of runtime controller state;
  no `isMoving` field in `ProjectDocument` / `PlatformerControllerComponent`.
- Editor catalog is registry-driven; no hard-coded typeId list in RmlUi.
- Host / World / Logic / Script share one query path (parity Editor Play,
  `game.exe`, WASM).
- Roadmap §2B.1: ship `platformer.motion_state`; keep forbidding typeId
  `is_moving`; treat prior “intent-based is_moving_horizontally” note as
  superseded for Walk/Idle (intent may return later as a distinct block).

## Verification

- `|vx| > ε` → Moving; release + decel below ε → Stopped once.
- OncePerActivation: Walk fires once on Stopped→Moving; not every tick while
  Moving; Idle once on Moving→Stopped.
- Right→Left without stop: Moving stays true → Walk does not restart; only
  facing changes.
- Airborne + Stopped + no Is Grounded gate must not force Idle over Jump/Fall
  when authors follow the canonical recipe (Grounded IF).
- Object Type without PlatformerController → incompatible block / repairable.
- Invalid `state` string fails validation.
- Collapsed summary shows Moving/Stopped.
