# ADR-0016 — Platformer State (locomotion projection)

**Status:** Accepted  
**Date:** 2026-07-23  
**Updated:** 2026-07-31 — Climbing / WallSliding  
**Scope:** Logic Board `platformer.motion_state`, `World::platformerState`,
`IGameplayRuntimeHost`, Logic codegen / Script SelfProxy, deprecation of
`platformer.is_falling` as a separate catalog condition  
**Supersedes:** [ADR-0015](ADR-0015-platformer-motion-state.md) (Motion =
Moving/Stopped only; typeId retained)  
**Related:** [`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md) §2B.1,
[ADR-0012](ADR-0012-platformer-move-direction.md),
[ADR-0052](ADR-0052-platformer-ground-support-side-contact.md),
[ADR-0055](ADR-0055-platformer-contact-logic-board.md), Constitution (single
authority; no runtime→authoring sync)

## Context

ADR-0015 shipped a Level condition for horizontal motion so Walk/Idle could
follow `|vx|` instead of key release. Authors need **Jump** / **Fall** /
**Climb** / **Wall Slide** clips driven by the same physical controller,
without parallel booleans or composing contact queries for every anim rule.

Contact edges (Landed, wall sides, ceiling) belong to
`PlatformerContactProjection` (ADR-0055), not this enum.

## Decision

### Canonical runtime enum

```cpp
enum class PlatformerState {
    Stopped,
    Moving,
    Jumping,
    Falling,
    Climbing,
    WallSliding,
};
```

Single authoritative projection:

```text
PlatformerRt (velocity, grounded, climbing, lastAirState)
+ wall-slide publish flag (intent + current X block)
        ↓
World::platformerState(id)
        ↓
IGameplayRuntimeHost::platformerState(owner)
        ↓
context.self:platformer_state()
// "Stopped"|"Moving"|"Jumping"|"Falling"|"Climbing"|"WallSliding"
```

No authoring fields on `PlatformerControllerComponent`. No parallel Logic
Board state machine. Persistent Logic strings are PascalCase (reject
`wall_sliding`, `Wall Sliding`).

### Resolution (+Y down)

Priority (final post-Y / post-support state):

1. Climbing → **Climbing**
2. Grounded + `|vx| > ε_h` → **Moving**
3. Grounded → **Stopped**
4. WallSliding publish flag → **WallSliding**
5. Airborne + `vy < -ε_v` → **Jumping** (update `lastAirState`)
6. Airborne + `vy > +ε_v` → **Falling** (update `lastAirState`)
7. Airborne + `|vy| ≤ ε_v` (apex) → **`lastAirState`** (never a false Stopped)

`lastAirState` stores only Jumping/Falling. Never Climbing or WallSliding.
While WallSliding with `vy > ε_v`, `lastAirState` may become Falling so the
airborne baseline restores when the mode ends.

### Climbing sticky

Once engaged (`onLadder` + `|climbAxis| > 0`), Self remains Climbing until it
leaves the Interaction sensor, jumps, wall-jumps, or another explicit detach.
Zero climb input with continued overlap does **not** clear climbing.

### WallSliding

Published only when all hold for the fixed step:

- next-step Wall Slide intent pending with finite `maxFallSpeed >= 0`;
- current `xMove` blocked on the **same** side as the intent;
- not climbing, no ground support at clamp time, `vy >= 0`;
- final publish: still airborne and not climbing after Y/support.

Not equivalent to Is Touching Wall. Side mismatch or lost X block rejects the
intent for that step (no clamp, no WallSliding). Clamp uses `std::min`; the
mode is active even when `vy` was already below `maxFallSpeed`.

If wall slide was active pre-Y but Y lands, grounded wins → Stopped/Moving.

### Logic Board block (compatibility)

| Item | Value |
|---|---|
| Stable typeId | **`platformer.motion_state`** (unchanged) |
| Display name | **Platformer State** |
| Property | `state`: Stopped \| Moving \| Jumping \| Falling \| Climbing \| WallSliding |
| Kind | Condition, Level (WHEN-eligible), OncePerActivation rising-edge |
| Requires | `PlatformerController` + Self |
| Feature | `platformer.motion_state` |

No ProjectDocument / Logic apiVersion bump — options only. Existing boards
with Moving/Stopped/Jumping/Falling need no migration.

Codegen:

```lua
context.self:platformer_state() == "Jumping"
```

### Canonical authoring recipe

```text
While Key Held D → Move Horizontal Right, Flip Right
While Key Held A → Move Horizontal Left, Flip Left
Platformer State Moving  · once → Play Walk
Platformer State Stopped · once → Play Idle
Platformer State Jumping · once → Play Jump
Platformer State Falling · once → Play Fall
Platformer State Climbing · once → Play Climb
Platformer State WallSliding · once → Play Wall Slide
Key Pressed W → Jump          -- request only; anim from Jumping state
```

Do **not** use Every occurrence for Play Clip while the state remains true.
Do **not** add Is Climbing / Is Wall Sliding conditions (duplicates of State).

`Moving` / `Stopped` imply grounded; Climbing is separate.

### Deprecation of `platformer.is_falling`

- **Catalog:** hide `Is Falling` (`catalogHidden`).
- **Load:** rewrite `platformer.is_falling` + expected true → State Falling.
- **Host:** `isFalling()` = `platformerState() == Falling` only
  (WallSliding is not Falling).
- **`isPlatformerMoving`:** `platformerState() == Moving` only
  (Climbing is not Moving).
- **`Is Grounded`:** remains — contact for non-anim logic.

### Host / Script

```cpp
virtual PlatformerState platformerState(EntityId owner) = 0;
```

Prefer `platformer_state()` as the primary query.

## Consequences

- One mutually exclusive locomotion state for anim selection.
- Contact edges stay in `PlatformerContactProjection` (ADR-0055).
- Apex hysteresis via `lastAirState` avoids Idle flicker mid-jump.
- Legacy predicates stay strict aliases of State values.

## Verification

- D hold/release: Stopped↔Moving, Walk/Idle once each.
- Jump: → Jumping once; apex: no Stopped frame; → Falling once; land Stopped
  or Moving depending on held direction.
- Ladder engage / sticky / jump detach → Climbing then Jumping/Falling.
- Wall Slide intent + matching X block → WallSliding; lost/mismatched side →
  Falling; land same tick → Stopped/Moving.
- Editor Play = `game.exe` = WASM host parity.
