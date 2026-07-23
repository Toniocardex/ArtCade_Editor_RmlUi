# ADR-0013 — Logic Board validation purposes (Structural / Authoring / Executable)

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** `logic-core` validation API, Logic Board Commands
(`COMMIT_NEXT_BOARD`), project Save/Load / `ProjectValidator`, Logic Board
panel diagnostics, Generated Lua, Start Play / Build / Export  
**Related:** [LOGIC_BOARD_RULES_ROADMAP](LOGIC_BOARD_RULES_ROADMAP.md)
(§2D.2 Authoring/Executable history), Constitution §21 (Lua / Logic Board),
Gates (authority + Play gate)

## Context

Today every Logic Board Command builds a candidate board correctly, then
`COMMIT_NEXT_BOARD` rejects the mutation if
`Logic::validateBoard(..., ValidationMode::Authoring)` reports **any**
`Error`:

```text
copy board → mutate → validationError(Authoring) → replaceLogicBoard
```

`LB_INCOMPATIBLE_BLOCK` is always an Error, including under Authoring. After
`RemoveTopDownControllerCommand` (which does not touch the board), N Top Down
Move actions remain in `ProjectDocument`. Delete/replace of a **single** rule
or action still fails because the other N−1 errors keep the whole board
“invalid.”

That is a **recovery deadlock**: the UI already exposes remove/replace/disable
paths, but the Command contract forbids incremental repair.

A second trap exists on Load: deserialize / `ProjectValidator` also reject
Authoring Errors. A board that was savable in-session can become
**unloadable** after restart.

`ValidationMode::{Authoring, Executable}` already exists, but Authoring only
demotes a few draft cases (explicitly empty asset/variable selections). It
does **not** separate structural safety from semantic repairability, and
Commands treat every Authoring Error as a commit veto.

## Decision

Freeze three distinct board states and one shared validator with an explicit
purpose:

| State | Meaning |
|---|---|
| Structurally valid | Safe to hold in `ProjectDocument`, Save, Load, mutate |
| Semantically incomplete / incompatible | Visible diagnostics; still editable |
| Not executable | Play / Generated Lua / Build / Export blocked until fixed |

**Clause (binding):**

> A semantic diagnostic may block execution, but must never block the edit
> required to correct it.

### Single validator, three purposes

Replace the overloaded Authoring-as-commit-gate with:

```text
enum class LogicValidationPurpose {
    StructuralCommit,      // Command + Save/Load structure
    AuthoringDiagnostics,  // Panel / Problems (non-blocking)
    Executable             // compileBoard, Play, Build, Export, Generated Lua
};
```

One `logic-core` implementation. Severity and which checks run depend on
`purpose`. Do **not** invent separate CommandValidator / LoadValidator /
PanelValidator / CompilerValidator implementations.

| Purpose | Command | Save/Load | Panel | Generated Lua | Play/Build |
|---|---|---|---|---|---|
| StructuralCommit | Yes | Yes | No | No | No |
| AuthoringDiagnostics | No | Post-load | Yes | No | Yes (optional mirror) |
| Executable | No | No | Optional | Yes | Yes |

### StructuralCommit — commit / persist gate only

Blocks only data that would corrupt or make the document unmanipulable:

- empty / duplicate rule ids;
- incoherent indices or out-of-range addresses;
- malformed property storage / non-serialisable variants;
- hard limit breaches (`kMaxRulesPerBoard`, actions/conditions caps, …);
- impossible internal references (e.g. addressing a missing action);
- invalid JSON / model structure on load.

**Must allow** (persist and Command):

- missing required gameplay component;
- missing asset / clip / variable;
- block type unknown to the current catalog (load, show typeId fallback, delete);
- semantically incomplete properties;
- rule with zero actions;
- block incompatible with the current Object Type.

### AuthoringDiagnostics — visible, non-vetoing

Shown in Logic Board / Problems. Severity may be Error or Warning for UX, but
Commands and Save/Load **must not** treat these as persistence vetoes.
Examples: `LB_INCOMPATIBLE_BLOCK`, missing asset/variable, `LB_ACTION_REQUIRED`,
`LB_UNKNOWN_BLOCK`.

### Executable — run gate

Used by `compileBoard`, Generated Lua, Start Play, Build, Export.

- Enabled rule with incompatible / unresolved semantics → **blocking**.
- Disabled rule with the same semantic issues → **authoring warning only**;
  must not block Play (compiler already skips `!rule.enabled`; Executable
  validation must share that semantics).
- Generated Lua with blocking diagnostics → **no** executable source and an
  explicit diagnostic list (never partial code presented as valid).

### Command contract

All Logic Board Commands (not only remove-rule) use StructuralCommit:

```text
copy current board
  → apply mutation
  → StructuralCommit
  → replaceLogicBoard
  → revision / dirty / DomainChange
```

No special bypasses per Command. Picker still prevents **introducing** a new
incompatible block type; a **persisted** incompatible block stays visible,
selected, replaceable, and deletable.

### Save ≠ Build

| Path | Requirement |
|---|---|
| Save / Load | StructuralCommit only for Logic |
| Play / Build / Export / Generated Lua | Executable |

### No silent auto-repair

Removing a component must **not** automatically rewrite rules, actions,
conditions, Generated Lua, or Scripts. Dependency UX and bulk repair are
explicit follow-up slices.

## Implementation slices

### Slice 1 — P0 Recovery (required before further Logic Board work)

1. Introduce `LogicValidationPurpose` (or equivalent rename of
   `ValidationMode`) in `logic-core`.
2. Change `COMMIT_NEXT_BOARD` to StructuralCommit only.
3. Load / deserialize Logic boards with StructuralCommit; run
   AuthoringDiagnostics **after** the document is loaded.
4. Align `ProjectValidator` with the same purpose (no duplicated Logic rules).
5. Keep Executable as the gate for Play / Generated Lua / Build.

**P0 tests**

- 8 incompatible actions → delete one rule → Command ok → revision +1 → 7
  diagnostics remain.
- 8 incompatible actions → replace one action → Command ok → 7 remain.
- Incompatible board → Save → reload succeeds → diagnostics still present.
- Incompatible board → Play rejected → document unchanged.
- Unknown block typeId → load ok → fallback label → delete ok.

### Slice 2 — Zero actions

- Allow `rule.actions.empty()` structurally and in UI (enable delete on last
  action; THEN empty state + Add Action).
- Active empty rule → `LB_ACTION_REQUIRED` Error under Executable.
- Disabled empty rule → authoring warning; does not block Play.
- Compiler: Executable remains authority; optional defensive skip is hardening
  only.

### Slice 3 — Component dependency preflight + bulk repair

- Canonical `collectComponentLogicReferences(...)` feeds dialog, Problems,
  badge, tests.
- Remove controller: Cancel (default) / Remove and Keep Logic / Review
  References — **no** automatic rule deletion; destructive bulk is review-only.
- One atomic `RepairIncompatibleLogicCommand` (Disable rules / Remove actions /
  Remove rules) with full board snapshot Undo — never N independent Commands.

### Slice 4 — Diagnostic navigation

Stable address (`objectTypeId`, `LogicRuleId`, target, block index,
`propertyKey`). Click opens the board, expands the rule, scrolls and
highlights. Authority is `LogicRuleId`, not visual order.

## Consequences

- Incomplete / incompatible boards remain first-class `ProjectDocument` data;
  diagnostics are derived projections.
- Catalog evolution must not make unknown blocks unloadable.
- Roadmap §2D.2 “Command consumes Authoring Errors as veto” is superseded for
  semantic codes; StructuralCommit replaces that gate.
- Follow-up UX (dependency dialog, zero-actions empty state, navigate-to-error)
  depends on Slice 1 landing first.

## Stop-the-line

Do not accept:

- a semantically invalid board that cannot be edited;
- full semantic validation as a precondition of every Command;
- delete/replace blocked by diagnostics on **other** rules;
- silent auto-deletion or TopDown→Platformer conversion on component remove;
- last-action delete disabled with no repair path;
- N Commands for one bulk repair;
- divergent validators in UI / Commands / compiler;
- Generated Lua as a second authority.

## Definition of Done (full plan)

- Incompatible board always editable; incremental delete/replace works.
- Save/Reload preserves incomplete boards; Play/Build still block active
  executable errors.
- Disabled semantically-invalid rules do not block Play.
- Last action removable; zero actions has a clear empty state.
- Component removal shows references; no automatic rule wipe.
- Bulk repair = one Undo; diagnostics navigate to the exact block.
- Command, Load, and compiler share one validation service.
