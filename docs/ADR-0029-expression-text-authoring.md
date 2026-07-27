# ADR-0029 — Number Expressions Are Authored As Text

**Status:** Accepted — authoring UI shipped; scalar `NumberExpression` disk
migration still remaining (see Implementation status)
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
- Volume and playback speed — **corrected after reading the sinks.** This ADR
  originally claimed "the runtime clamps when it is not [a literal]; clamping is
  the existing behaviour of both sinks". That is wrong on both counts. Neither
  sink clamps: `Audio::playResolvedAsset` returns false for a volume outside
  0..1, and `World::setAnimationPlaybackSpeed` returns false for a speed <= 0.
  Both falses become a thrown `sol::error` in the Logic binding, and a throw
  from a callback deactivates the subscription — so an out-of-range dynamic
  value does not degrade, it **disables the rule**. That is worse than either
  clamping or staying literal.

  The fix is not to change the sinks — they are shared with Lua scripting and
  rejecting a nonsense argument is reasonable there. It is to clamp in the
  *generated* Lua for the dynamic case, so the author gets the value they asked
  for, squeezed into the legal range, and the rule keeps running. The literal
  case keeps its authoring-time diagnostic, which is strictly better feedback
  than a silent clamp.

  Until that codegen exists, volume and playback speed stay LiteralOnly.

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
| The value field as the editor: typing, completions, inline errors | Written `2cf5770`, **reachable only from `b3a3ad0`** — see below |
| Modal, node tree, palette, Apply/Cancel draft removed | Done (`cb607e3`) |
| Vec2 policy + codegen: `translate_by.offset`, `set_velocity.velocity` | Done (`14b1c18`) — expression field via generic property editor |
| Vec2 still `LiteralOnly`: `set_scale.scale`, `spawn.position` | Unchanged (correct for LiteralOnly) |
| Nine scalar Number parameters (policy / editor / disk arm) | **Not started** — needs schema 5 / format 12 migration |
| Logic Board schema 5, project format 12 | **Not started** |

Set Position, Move By, and Set Velocity offer the expression text field.
Set Scale and Spawn keep plain numeric axes. The nine scalars still need the
indivisible `LogicValue` NumberExpression arm + schema bump described below.

### The field was written before it worked

This row read "Done" for two weeks while the completion list had never once
appeared in a shipped build. The row is kept, corrected, rather than quietly
fixed, because the way it was wrong is the useful part.

The focus event never reached the panel: an unconditional `if (type ==
"focus") return;` in the router, written for the commit fields' pre-edit
baseline, sat above the branch that ADR-0029 added later and swallowed every
focus. Everything downstream was correct and unreachable — including the
typing filter, since the draft setter is guarded by the focus address that was
never set. The only way to see the list was to commit text that failed to
parse, which sets the address as a side effect.

Nothing caught it because every test called
`handleAction("focus-logic-expression", …)` directly, below the router, and
the screenshot harness invoked the action rather than focusing the element —
so it photographed a state the application could not produce. Both now start
from the element (`tests/logic-expression-focus-routing-test.cpp`,
`--shot-expression`).

Making the event reachable then exposed three defects that had never run:

- **Rebuilding the panel inside an RmlUi input dispatch is a use-after-free.**
  `Element::Focus()` keeps using `this` after `Context::OnFocusChange`
  returns, and the text widget keeps using its element after emitting
  `change`. RmlUi guards its own dispatch loop with observer pointers; it does
  not guard the caller. Focus, draft, and the end of an edit (Enter, Escape,
  blur) are therefore recorded on the event and applied in `processFrame`.
- **A rebuild blurs the field it destroys**, which the router could not tell
  from the author leaving it. Committing on that blur cleared the state the
  rebuild was rendering, so the list cancelled itself. The question "is this
  our own rebuild?" must be asked when the event fires, not when the deferred
  action runs — by then the rebuild has finished and the answer is always no.
- **Typing must not rebuild the panel at all.** A keystroke now replaces the
  completion entries and nothing else. Rebuilding threw away the element being
  typed into, and the caret with it, so Backspace landed at offset 0 and
  deleted nothing: the field looked editable and refused to edit. The caret is
  captured and restored across the rebuilds that remain.

The durable rule: **no panel rebuild inside an RmlUi input dispatch, and no
markup rebuild of a field while it is being typed into.**

### Known gap

An uncommitted draft is not resolved before Save / Open / New / Play.
`resolvePendingEdits()` only inspects a focused element whose `data-action`
starts with `commit-`, and this field declares `edit-logic-expression`, so the
guard skips it entirely. Gates §15 requires the opposite. Tracked as the next
slice.

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

It also becomes *readable*. The `Code` formatter first wrapped every binary
node — sound for a round trip that does not want to reason about precedence,
invisible while `Code` was a serialisation detail, and wrong once the field
shows that text: `self.x + 10` came back as `(self.x + 10)`, with a pair added
per nesting level. It now tracks binding strength and keeps only the
parentheses the tree needs, which are the ones an author would have written
(`56d0e1d`). The round trip is unchanged and still exercised over the whole
node catalogue.

The draft is deliberately **not** normalised as the author types. A
half-written expression usually has no tree to format, so the text would snap
only on the keystrokes that happen to parse; and rewriting text under the
caret is the defect above, reintroduced on purpose.
