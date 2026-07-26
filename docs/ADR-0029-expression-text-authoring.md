# ADR-0029 — Number Expressions Are Authored As Text

**Status:** Proposed
**Date:** 2026-07-26
**Scope:** Canonical expression syntax, parser, the value field as the editor,
per-descriptor expression policy
**Supersedes:** the authoring half of [ADR-0028](ADR-0028-logic-number-expressions.md)
(structured node editing; `entity.set_position` as the only expression site).
The AST, ownership and runtime contract of ADR-0028 stand unchanged.
**Related:** ADR-0027 (UI design tokens), Constitution §21

## Context

ADR-0028 shipped typed number expressions with a structured editor: an `fx`
button on the value row, a modal, and a tree of nodes each carrying a "Change"
button that opens a palette of every node type.

Used, it does not hold up:

- **The row carries two controls for one action.** In the expression state the
  summary box and the `Edit` button emit the same `data-action` with the same
  `data-arg`. One of them is pure duplication.
- **The two states of one parameter are two different controls** — an editable
  field when literal, a button when dynamic — with different heights (26dp vs
  28dp), which is the visible misalignment between the X and Y rows.
- **There is no way back to a literal from the row.** Reverting `Random(0, 1)`
  to `0` requires opening the modal and re-picking the "Number" node.
- **A full-screen modal edits one number**, with the content occupying roughly
  the top fifth of the dialog.
- **The palette is 20 buttons shown at once**, no search, and opening it pushes
  the footer down. It does not scale as node types are added.
- **The commit model is inconsistent** with the rest of the Logic Board: a draft
  with Apply/Cancel, where every other control commits a command per
  interaction.

The tree UI exists only because there was no way to *write* an expression. That
is the assumption worth removing.

## Decision

### The value field is the editor

Every expression-capable numeric parameter is a single text field — the same
`.logic-value-input` as every other value in a rule card. Typing `100` stores a
literal; typing `random(0, 100)` or `self.x + 10` stores an expression. There is
no `fx` badge, no `Edit` button, no modal, and no node tree; all are removed.

Reverting to a literal costs nothing: clear the field and type a number.

**Autocomplete opens on focus, not only after a keystroke.** This is what
replaces the palette as the discovery surface, so it must be reachable without
knowing what to type. It lists context values, functions and project variables,
filtering as the author types.

**Autocomplete renders in flow, not as a popup.** The Logic Board panel scrolls
and clips absolutely-positioned popups; this is the same constraint that already
forced the property dropdowns in flow. The Script Editor's completion machinery
is the model, not its positioning.

A parse failure shows an inline diagnostic under the field and **never discards
what the author typed**.

### Canonical syntax

The existing `Compact` / `Full` styles emit prose — `Self X`, `Scene Width`,
`Delta Seconds` — which cannot be parsed unambiguously against a variable of the
same name. A third style, `Code`, is added as the round-trip form:

```
expr     := term (('+' | '-') term)*
term     := factor (('*' | '/') factor)*
factor   := '-' factor | primary
primary  := number | context | variable | call | '(' expr ')'
context  := 'self.x' | 'self.y' | 'scene.width' | 'scene.height' | 'delta'
variable := '$' varname                         -- Local scope
          | '$global.' varname                  -- Global scope
varname  := identifier | "'" any "'"            -- quoted when not an identifier
call     := fn '(' expr (',' expr)* ')'
fn       := 'min' | 'max' | 'abs' | 'floor' | 'ceil' | 'round'
          | 'clamp' | 'lerp' | 'random'
```

The grammar is closed and typed. It is **not** Lua and not a general expression
language: it parses to exactly the ADR-0028 AST and nothing else, so the
safety argument of that ADR is unchanged — an author still cannot reach the
interpreter.

`parseNumberExpression` and `formatNumberExpression(…, Code)` are mutually
inverse. That is an enforced invariant, not a convention:
`parse(format(e, Code)) == e` for every expression, exercised over the whole
node catalogue.

### Which parameters become expression-capable

Every numeric and Vec2 parameter, except where a dynamic value would change
runtime semantics or defeat a check that must be static.

| Parameter | Policy | Reason |
|---|---|---|
| `entity.set_position.position` | Expression | Already, per ADR-0028 |
| `entity.translate_by.offset` | Expression | Evaluated per invocation |
| `entity.set_rotation.degrees` | Expression | " |
| `entity.rotate_by.degrees` | Expression | " |
| `entity.set_scale.scale` | Expression | " |
| `entity.spawn.position` | Expression | " |
| `physics.set_velocity.velocity` | Expression | " |
| `state.set.value` | Expression | " |
| `state.add.amount` | Expression | " |
| `state.subtract.amount` | Expression | " |
| `state.compare.value` | Expression | Condition operand, evaluated per event |
| `flow.wait.seconds` | Expression | Read when the wait is *started*, so a dynamic value is meaningful |
| `animation.set_playback_speed.speed` | Expression | Evaluated per invocation; see range rule below |
| `audio.play_sound.volume` | Expression | " |
| **`system.every_seconds.seconds`** | **LiteralOnly** | Consumed at subscription *registration*, not per call: the runtime stores `intervalSeconds` on the Subscription when the board installs. An expression there would be evaluated once and silently frozen — the value would look dynamic and not be. Excluded until the scheduler can re-evaluate it, which is a runtime change, not a UI one. |

### Static range checks meet dynamic values

Three parameters carry a static range check today: `LB_TIMER_INTERVAL`
(seconds > 0), `LB_AUDIO_VOLUME_RANGE` (volume in 0..1) and animation playback
speed (positive).

The rule: **a check that can only be decided statically keeps the parameter
literal; a check that can be enforced at runtime moves there.**

- Timer interval — the parameter stays literal, so the check is unaffected.
- Volume and playback speed — the static check still runs whenever the value
  *is* a literal, and the runtime clamps when it is not. Clamping is the
  existing behaviour of both sinks, so this narrows nothing.

### Ownership

Unchanged from ADR-0028. `artcade-logic-core` gains the parser and the `Code`
style, next to the formatter and validator it already owns. `artcade-core` keeps
the AST. The runtime is untouched by this ADR except for the two clamps above.
The editor owns the field, the completion list and the inline diagnostic — and
no grammar: it never parses expression text itself, it calls logic-core.

### Versions

Number-valued properties gain an expression arm on disk, so a board written
under this ADR cannot be read by a reader expecting schema 4.

| Authority | Value |
|---|---|
| Logic Board schema | 5 |
| Editor / runtime project format | 12 |
| Export template min / max | 12 / 12 |
| Logic API | 2 (unchanged — no new runtime binding) |

## Implementation status

The decisions above are the target. What is actually in the code, so the table
is not mistaken for the state of the world:

| Part | State |
|---|---|
| `Code` style, parser, round-trip test, completion vocabulary | Done (`ea062e8`) |
| The value field as the editor: typing, completions, inline errors | Done (`2cf5770`) — **on `entity.set_position.position` only** |
| Modal, node tree, palette, Apply/Cancel draft removed | Done (`cb607e3`) |
| The other 13 parameters in the policy table | **Not started** |
| Logic Board schema 5, project format 12 | **Not started** |

Every parameter except Set Position still renders the plain numeric field. That
is correct behaviour for `LiteralOnly`, so nothing is broken — but the table
above describes an intent for those rows, not shipped behaviour.

The 13 split into two kinds of work. Four are Vec2 — `translate_by.offset`,
`set_scale.scale`, `spawn.position`, `set_velocity.velocity` — which already
store `LogicVec2Value` structurally, so they need only the policy flipped and
codegen taught to compile a non-literal instead of skipping it. Nine are
scalars — `set_rotation.degrees`, `rotate_by.degrees`, `state.set.value`,
`state.add.amount`, `state.subtract.amount`, `state.compare.value`,
`wait.seconds`, `set_playback_speed.speed`, `play_sound.volume` — which store
`NumberExpression` since slice 4, and additionally need the editor's Number
field to accept text the way Set Position's does.

Three of the nine carry a static range check (volume, playback speed, and the
excluded timer interval); the rule for those is in the section above and is the
only part of the remainder that is a decision rather than a repetition.

The remaining work is one indivisible migration, not a flag: `LogicValue` has a
`double` arm and no `NumberExpression` arm, so scalar properties are
structurally unable to hold an expression today. Adding it changes what is
written to disk, so the variant arm, the JSON codec, the schema 4→5 migration,
the `LiteralOnly` enforcement, the eight codegen read sites and the format bump
have to land together or saved projects read back wrong. It deserves its own
pass, with the migration tested against real schema-4 files before anything
ships.

## Consequences

The `fx` button, the expression modal, the node tree and the type palette are
deleted, along with `number_expression_editor_controller` and its stylesheet.
`NumberExpressionEditorDraft` and the Apply/Cancel model go with them: every
edit becomes a command, undoable like the rest of the board.

**The cost is discoverability.** A palette shows what exists; a text field does
not. This is why autocomplete-on-focus is a requirement above and not a
refinement — if it regresses to filter-after-typing, the feature becomes
unusable for anyone who has not memorised the vocabulary.

**The formatter becomes load-bearing.** It is currently a display convenience;
under this ADR it is half of a round-trip, and a formatting change that is not
matched in the parser corrupts what the author typed. The round-trip test is the
guard, and it is not optional.
