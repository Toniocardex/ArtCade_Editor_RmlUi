# ADR-0016 — Platformer State (Stopped / Moving / Jumping / Falling)

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** Logic Board `platformer.motion_state`, `World::platformerState`,
`IGameplayRuntimeHost`, Logic codegen / Script SelfProxy, deprecation of
`platformer.is_falling` as a separate catalog condition  
**Supersedes:** [ADR-0015](ADR-0015-platformer-motion-state.md) (Motion =
Moving/Stopped only; typeId retained)  
**Related:** [`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md) §2B.1,
[ADR-0012](ADR-0012-platformer-move-direction.md), Constitution (single
authority; no runtime→authoring sync)

## Context

ADR-0015 shipped a Level condition for horizontal motion so Walk/Idle could
follow `|vx|` instead of key release. Authors still need **Jump** and **Fall**
clips driven by the same physical controller, without:

- four parallel booleans (`isMoving` / `isStopped` / `isJumping` / `isFalling`);
- composing `Is Grounded` + `Platformer Motion` for every anim rule;
- a one-frame **Stopped** flash at jump apex when `|vy| ≈ 0`.

`Is Falling` already exists as a separate Level condition. Keeping it beside a
unified state block duplicates authority and confuses the catalog.

## Decision

### Canonical runtime enum

```cpp
enum class PlatformerState {
    Stopped,
    Moving,
    Jumping,
    Falling,
};
```

Single authoritative projection:

```text
PlatformerRt (velocity, grounded, lastAirState)
        ↓
World::platformerState(id)
        ↓
IGameplayRuntimeHost::platformerState(owner)
        ↓
context.self:platformer_state()   // string: "Stopped"|"Moving"|"Jumping"|"Falling"
```

No authoring fields on `PlatformerControllerComponent`. No parallel Logic
Board state machine.

### Resolution (+Y down)

Priority:

1. Grounded (or climbing) + `|vx| > ε_h` → **Moving**
2. Grounded (or climbing) + `|vx| ≤ ε_h` → **Stopped**
3. Airborne + `vy < -ε_v` → **Jumping** (update `lastAirState`)
4. Airborne + `vy > +ε_v` → **Falling** (update `lastAirState`)
5. Airborne + `|vy| ≤ ε_v` (apex) → **`lastAirState`** (never a false Stopped)

`ε_h` / `ε_v` are module policy constants (not authoring properties). Update
`lastAirState` only during the platformer fixed step when Jumping/Falling is
detected — not from Logic.

### Logic Board block (compatibility)

| Item | Value |
|---|---|
| Stable typeId | **`platformer.motion_state`** (unchanged) |
| Display name | **Platformer State** (was Platformer Motion) |
| Property | `state`: **Stopped \| Moving \| Jumping \| Falling** (default Moving) |
| Kind | Condition, Level (WHEN-eligible), OncePerActivation rising-edge |
| Requires | `PlatformerController` + Self |
| Feature | `platformer.motion_state` |

Existing boards with `Moving` / `Stopped` need **no migration**.

Codegen:

```lua
context.self:platformer_state() == "Jumping"
```

Do not emit raw `velocity` or epsilon comparisons in board Lua.

Rule summary incorporates the state value, e.g. `Platformer Jumping → Play Clip`.

### Canonical authoring recipe

```text
While Key Held D → Move Horizontal Right, Flip Right
While Key Held A → Move Horizontal Left, Flip Left
Platformer State Moving  · once → Play Walk
Platformer State Stopped · once → Play Idle
Platformer State Jumping · once → Play Jump
Platformer State Falling · once → Play Fall
Key Pressed W → Jump          -- request only; anim from Jumping state
```

Do **not** use Every occurrence for Play Clip while the state remains true.
Do **not** put Play Jump on the key rule — input requests jump; state selects
the clip (also works for Script/platform launch).

`Moving` / `Stopped` imply grounded (or climbing); no extra `Is Grounded` IF
is required for the Walk/Idle recipe.

### Deprecation of `platformer.is_falling`

- **Catalog:** hide `Is Falling` from add-Event / add-Condition pickers
  (`catalogHidden`).
- **Load:** rewrite existing blocks `platformer.is_falling` + `expected`
  into `platformer.motion_state` with `state = Falling` (or Stopped when
  expected was false — rare; prefer Falling when expected true, and when
  expected false rewrite to a Level WHEN that is the negation via a different
  state is ambiguous — only migrate `expected == true` / default; leave
  `expected == false` with a deprecation diagnostic until manual repair).
- **Host:** keep `isFalling()` as `platformerState() == Falling` for Script /
  legacy codegen until boards are migrated.
- **`Is Grounded`:** remains — contact query for non-anim logic.

### Host / Script

```cpp
virtual PlatformerState platformerState(EntityId owner) = 0;
```

`isPlatformerMoving(owner)` becomes `platformerState(owner) == Moving` (thin
wrapper for Script `ctx.platformer.is_moving` parity). Prefer documenting
`platformer_state()` as the primary query.

## Consequences

- One mutually exclusive state for anim selection; Walk/Idle/Jump/Fall share
  one OncePerActivation edge model.
- ADR-0015 semantics for Moving/Stopped on ground are preserved; Jumping/Falling
  extend the same typeId.
- Apex hysteresis via `lastAirState` avoids Idle flicker mid-jump.
- Catalog complexity drops once Is Falling is hidden; old projects migrate on
  load for the common case.

## Verification

- D hold/release: Stopped↔Moving, Walk/Idle once each.
- Jump: → Jumping once; apex: no Stopped frame; → Falling once; land Stopped
  or Moving depending on held direction.
- Right↔Left without stop: stays Moving, Walk does not restart.
- Editor Play = `game.exe` = WASM host parity.
- Boards with only Moving/Stopped unchanged; `Is Falling` (expected true)
  migrates to State Falling.
