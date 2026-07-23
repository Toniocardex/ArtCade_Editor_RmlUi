# ADR-0012 — Platformer Move Horizontal Left/Right direction

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** Logic Board `platformer.move_horizontal`, editor property editor,
generated Logic Lua  
**Related:** [ADR-0005](ADR-0005-top-down-logic-input.md) (Top Down direction
dropdown), [ADR-0011](ADR-0011-platformer-timing-climb-fields.md)

## Context

`platformer.move_horizontal` asks authors for a free numeric `axis` in
`[-1, 1]`. Top Down movement already uses a closed **Left / Right / Up /
Down** choice. For the common left/right walk recipe, typing `-1` / `1` is
easy to get wrong and inconsistent with the Top Down UX authors already know.

The runtime host still wants a single float intent
(`requestPlatformerMove(owner, axis)`). Only authoring and codegen need to
change.

## Decision

Replace the authored property of **Move Horizontal** with a closed
**Direction** choice: **Left** | **Right** (default **Right**).

| Direction | Emitted Lua |
|---|---|
| `Left` | `context.self:platformer_move(-1)` |
| `Right` | `context.self:platformer_move(1)` |

- Stable typeId stays `platformer.move_horizontal`.
- New semantic `LogicPropertySemantic::PlatformerDirection` with options
  `{"Left","Right"}` (not reuse `TopDownDirection`, which includes Up/Down).
- Validation rejects any other direction string.
- **Legacy:** boards that still store numeric `axis` continue to compile:
  finite values in `[-1, 1]` emit `platformer_move(axis)` unchanged. New
  default blocks and the Inspector picker only offer `direction`.

## Consequences

- Authoring recipe mirrors Top Down: `While Key Held A → Move Horizontal Left`,
  `D → Right`, plus Jump / Flip as needed.
- No host / World API change.
- Editor dropdown uses the same options UI path as Top Down / Flip Facing.

## Verification

- Default Move Horizontal compiles to `platformer_move(1)` (Right).
- Left compiles to `platformer_move(-1)`.
- Legacy `{axis: -1}` still compiles.
- Invalid direction fails validation.
