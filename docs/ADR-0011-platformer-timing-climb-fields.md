# ADR-0011 — Author Platformer coyoteTime / jumpBuffer / climbSpeed

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** native editor Inspector, `ProjectDocument` Commands, editor project
JSON (`project_io`), Play materialisation of `PlatformerControllerComponent`  
**Does not change:** runtime platformer step algorithm
(`world_platformer_controller.cpp`), Logic Board catalog, Script API

## Context

`PlatformerControllerComponent` already owns six authored scalars in the
shared runtime type (`types.h`):

| Field | Default | Runtime role |
|---|---:|---|
| `maxSpeed` | 300 | horizontal speed cap |
| `jumpForce` | 600 | jump impulse |
| `customGravity` | 1500 | gravity scale |
| `coyoteTime` | 0.15 s | post-edge jump window |
| `jumpBuffer` | 0.1 s | pre-land jump press window |
| `climbSpeed` | 120 px/s | ladder climb rate |

The native editor today authors only the first three (Inspector labels
Move Speed / Jump Speed / Gravity). `project_io` serialises only those three
keys; on load the other three stay at C++ defaults even if a hand-edited or
runtime-exported project carried them.

The runtime already:

- reads all six from entity JSON (`entity-json.cpp`);
- applies `coyoteTime` / `jumpBuffer` / `climbSpeed` in
  `WorldInternal::stepPlatformerController`.

So the gap is **editor authority and persistence**, not gameplay behaviour.
Leaving the fields invisible means authors cannot tune feel without editing
JSON by hand, and editor save silently drops any values that arrived from
outside the three-field subset.

## Decision

Expose and persist the three missing fields as first-class Object-Type
authoring on `PlatformerController`, using the same path as the existing
scalars.

### Authority

| Data | Owner |
|---|---|
| Authored platformer scalars (all six) | `ProjectDocument` → `EntityDef.platformerController` |
| Inspector buffers | commit on Enter/blur only when valid and changed |
| Runtime during Play | materialised once from `ProjectDocument`; no reverse-sync |

RmlUi never mutates `ProjectDocument` directly. No new authority, polling, or
per-frame sync.

### Mutation flow

```text
Inspector numeric field
  → commit-* action (Enter/blur)
  → SetPlatformerValueCommand(PlatformerField, value)
  → ProjectDocument::setPlatformerValue
  → DomainChange::objectTypeComponentChanged(PlatformerController)
```

Extend `PlatformerField` (do not add three parallel Command classes):

| Enum | Component field | Inspector label | JSON key |
|---|---|---|---|
| `MoveSpeed` | `maxSpeed` | Move Speed | `maxSpeed` |
| `JumpSpeed` | `jumpForce` | Jump Speed | `jumpForce` |
| `Gravity` | `customGravity` | Gravity | `customGravity` |
| `CoyoteTime` | `coyoteTime` | Coyote Time | `coyoteTime` |
| `JumpBuffer` | `jumpBuffer` | Jump Buffer | `jumpBuffer` |
| `ClimbSpeed` | `climbSpeed` | Climb Speed | `climbSpeed` |

Validation: finite and `>= 0` for every field (same `NumericValidation`
contract as today). Invalid input does not commit; Undo restores the prior
scalar exactly.

### Persistence

Editor `objectTypeToJson` / object-type reader write and read **all six**
canonical keys. Missing keys on load keep component defaults (compatible with
projects saved before this ADR). No formatVersion bump: additive optional
keys under an existing object.

Runtime `entity-json` already reads the three timing/climb keys; no runtime
schema change is required for Play when the editor saves them.

### Play / Undo

- Play: materialise from document as today; changing Inspector while Playing
  remains blocked at the coordinator (UI disable is affordance only).
- Undo: one stack entry per committed field change (existing
  `SetPlatformerValueCommand` behaviour).
- Stop Play: destroy session; no reverse-sync of runtime timers into
  authoring.

### Out of scope

- Ladder authoring UI (`CollisionShapeRole::Interaction`) — climbSpeed only
  matters when such sensors exist; exposing the scalar does not invent
  ladder placement.
- Renaming Inspector “Jump Speed” → “Jump Force” (keep current labels).
- Script/Logic blocks for setting these fields at runtime (already partially
  covered by game-api property getters where present).

## Alternatives considered

1. **Leave defaults only** — rejects: feel tuning is a core platformer
   authoring need and the runtime already consumes the fields.
2. **Separate Commands per field** — more boilerplate; the existing
   `PlatformerField` + `SetPlatformerValueCommand` pattern already scales.
3. **Bump formatVersion** — unnecessary for additive keys with defaults.

## Consequences

- Inspector Platformer section gains three numeric rows.
- Saved projects round-trip all six scalars; older projects load unchanged.
- `isValid(PlatformerControllerComponent)` must include the three new fields
  so deserialize / Play reject NaN or negative values consistently.
- Docs that said “coyoteTime/jumpBuffer/climbSpeed keep defaults, not
  edited” (`platformer_controller_commands.h`, migration notes) are updated
  to match this ADR.

## Verification

- Command: set each new field, reject `< 0`, undo/redo.
- Serialize → deserialize preserves non-default coyoteTime / jumpBuffer /
  climbSpeed.
- Play uses the authored values (runtime already wired; regression via
  document materialisation is sufficient for this slice).
