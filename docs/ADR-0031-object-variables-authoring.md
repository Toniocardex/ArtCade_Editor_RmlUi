# ADR-0031 — Object Variables authoring (Slice A)

**Status:** Accepted
**Date:** 2026-07-26
**Scope:** authoring surface and mutation path for `ObjectType.localVariables`
(definitions) and `SceneInstanceDef.localVariableOverrides` (value overrides);
editor persistence of both fields; variable reference counting shared with
`globalVariables`
**Related:** Constitution (single authorities; RmlUi never mutates the document),
[Engineering Gates](ARTCADE_RMLUI_ENGINEERING_GATES.md) § Intent/Command,
§ staged mutation, § input buffers commit on Enter/blur,
[ADR-0017](ADR-0017-coordinator-document-core-boundaries.md),
[ADR-0020](ADR-0020-scene-background-color-inspector.md) (Inspector section +
draft-buffer precedent), [ADR-0028](ADR-0028-logic-number-expressions.md),
[ADR-0029](ADR-0029-expression-text-authoring.md)

## Context

The three-level variable ownership already exists in the project format v10 and
in the runtime. Nothing here invents an authority:

| Data | Owner | Evidence |
|---|---|---|
| Project variables | `ProjectDoc.globalVariables` | `core/types.h:930` |
| Object variable definitions | `ObjectType.localVariables` | `core/types.h:549` |
| Instance value overrides | `SceneInstanceDef.localVariableOverrides` | `core/types.h:720` |

`object-type-materialize.cpp:89` copies the instance overrides onto the
materialised entity over the prototype's definitions, and
`presentation_variable_refs.cpp:114` already resolves the effective authored
value as *override if present, else the type's `initialValue`*. That resolution
rule is the contract; the Inspector must present it, not restate it.

Four findings from the code changed the shape of this slice.

**1. There is no authoring path for object variables — at all.**
At HEAD, `commands/` contains only `global_variable_commands.{h,cpp}`. The
runtime reads local variables, number expressions resolve
`NumberVariableScope::Local` (`logic_property_editor.cpp:130`), Text/Gauge
bindings accept `TextBindingScope::Local` — but no editor Command can create
one.

The Text/Gauge work in progress adds `local_variable_commands.{h,cpp}`
(uncommitted at the time of writing) with `RenameObjectTypeLocalVariable`,
`RemoveObjectTypeLocalVariable`, `SetObjectTypeLocalVariableType` and a
reference counter. Those exist so a rename can follow Text/Gauge `bindKey`s,
not as an authoring surface: there is no Add, no initial value, no
description, and no instance-override command. The void stands.

They do introduce a third naming convention. **Decision: one family,
`ObjectVariable…`**, matching `AddGlobalVariableCommand` /
`SetGlobalVariableInitialValueCommand`. A1.1 absorbs the three WIP commands
into that family rather than leaving `Global…`, `ObjectTypeLocalVariable…`
and `ObjectVariable…` side by side.

**2. The editor drops both fields on load and save.**
`model/project_io.cpp` reads and writes `globalVariables` only. Its hand-rolled
`objectTypes` reader (`project_io.cpp:1426`) and `objectTypeToJson`
(`project_io.cpp:783`) / `instanceToJson` (`project_io.cpp:684`) never mention
`localVariables` or `localVariableOverrides`, while the runtime's
`entity-json.cpp:267` does read them. A project authored elsewhere loses those
fields the first time the editor saves it. Slice A is therefore **domain +
persistence**, not domain alone.

**3. Reference counting is blind to number expressions.**
At HEAD the walker visits `LogicVariableReference` property values and nothing
else; the Text/Gauge extension of `referencesIn` lives in the same uncommitted
work in progress as finding 1, so the committed baseline A1.0 starts from is
the narrower one. Neither version walks `NumberVariableExpression`
(`core/logic-number-expression.h:74`) — `variableId` appears nowhere under
`src/editor-native`. The same restricted traversal also backs the type-change
compatibility check. Consequences today, for **project** variables:

- `$Score` used only inside an expression counts as 0 references, so Delete is
  offered and accepted, leaving a dangling node;
- Rename rewrites property references — and, in the work in progress, bindings
  — but never the expression node, which then names a variable that no longer
  exists.

An object-variable delete guard copied from that code would be decorative:
expressions are the *primary* consumer of local number variables. The walk must
be written once, generic over scope, and used by both variable families.

**4. Object Types are not reachable without an instance.**
`inspector_panel.cpp:507` selects Scene mode whenever the active scene holds no
selected instance; every `OBJECT TYPE`-badged section
(`inspector_panel.cpp:929`, `:1057`, `:1204`) is reached *through* an instance.
There is no `selectedObjectTypeId` anywhere. § Reachability decides this.

## Decision

### Authority

| Concern | Owner | Mutation |
|---|---|---|
| Object variable definition (key, type, initial value, description) | `ProjectDocument` → `ObjectType.localVariables` | Object variable Commands only |
| Instance override value | `ProjectDocument` → `SceneInstanceDef.localVariableOverrides` | override Commands only |
| Effective authored value | derived, never stored | `resolveEffectiveBoundInitialValue` rule |
| Inspector row state mid-edit | `InspectorPanel` draft buffer | never persisted |

The Inspector is projection plus buffer. No parallel catalog, no mutable copy of
a definition list, no `EditorState` mirror of variable values.

### Lexicon (user-visible)

`Project Variables` · `Object Variables` · `Object Type default` ·
`Instance override`. "Local" and "Global" disappear from authoring UI. The
expression completion list already says `Object variable` / `Project variable`
(`logic_property_editor.cpp:131`), and the drawer is already titled
`Project Variables` — this is alignment, not a new vocabulary.

Field and JSON names stay `localVariables` / `localVariableOverrides`: they are
the v10 contract shared with the runtime. Lexicon is a presentation decision.

### A1 — domain, Commands, persistence (no UI)

A1 stays one architectural unit but ships as two ordered commits, so the
behaviour change to an already-released feature is legible in history rather
than buried inside a new feature:

```text
A1.0  shared variable reference walk
      + GlobalVariableCommands migrated onto it
      + regression test for the expression-only reference

A1.1  ObjectVariableCommands
      + override lifecycle (rename / delete / retype)
      + persistence of definitions and overrides
```

A1.0 changes what the shipped Delete does: a project variable used only inside
an expression was accepted before and is refused after. That belongs in its own
commit description.

Names mirror the existing global family exactly, including
`InitialValue` (the field is `GameVariableDefinition::initialValue`; "default
value" would introduce a second word for one concept):

```text
AddObjectVariableCommand(ObjectTypeId, GameVariableDefinition)
RemoveObjectVariableCommand(ObjectTypeId, GameVariableId)
RenameObjectVariableCommand(ObjectTypeId, GameVariableId from, GameVariableId to)
SetObjectVariableTypeCommand(ObjectTypeId, GameVariableId, Type)
SetObjectVariableInitialValueCommand(ObjectTypeId, GameVariableId, GameVariableValue)
SetObjectVariableDescriptionCommand(ObjectTypeId, GameVariableId, std::string)

SetInstanceVariableOverrideCommand(SceneId, EntityId, GameVariableId, GameVariableValue)
ClearInstanceVariableOverrideCommand(SceneId, EntityId, GameVariableId)
```

All follow the staged shape already used by the global family
(`global_variable_commands.cpp:137`): copy `ProjectDoc`, mutate, validate,
`commitStagedCommand`. A composite mutation that fails mid-way therefore cannot
leave the document partially written — the gates' staging requirement is met by
the existing pattern, not by new machinery.

**Invariants**

1. Key unique within the owning Object Type (project and object scopes may
   legitimately reuse a name; they are different namespaces).
2. Key non-empty; same character policy as project variables.
3. `initialValue` alternative matches `type`; numbers finite (no NaN/Inf).
4. An override may exist only for a key defined on that instance's Object Type,
   and its alternative must match the definition's `type`.
5. An instance never introduces a definition.
6. Rename preserves identity: the definition is updated in place, never
   removed-and-re-added.

**Type change.** Same policy as `SetGlobalVariableTypeCommand`: reject when a
reference requires a different type, else reset `initialValue` to the type's
deterministic default. Additionally — and this is new, because project variables
have no per-instance state — the command **drops every now-incompatible override
of that key on every instance in every scene**, capturing them for undo. Silent
retyping of override values is not attempted.

**Delete.** Blocked while references exist, with the count reported in the
message, mirroring `RemoveGlobalVariableCommand`. When it proceeds it also
removes that key's overrides from every instance of the type, captured for undo.

**Rename.** Atomically rewrites, within one staged document:
logic property `LogicVariableReference`s in that type's own board ·
`NumberVariableExpression` nodes with `scope == Local` in that board ·
Text/Gauge bindings with `TextBindingScope::Local` owned by that type ·
**the override map keys on every instance of the type**. The last one is easy to
forget and produces orphan overrides that survive save/load.

**Shared reference walk.** One module (`model/variable_references.*`) exposes
count / rename / type-compatibility, parameterised by scope and owner — the
same shape `presentation_variable_refs.h` already uses (`TextBindingScope`,
`const ObjectTypeId*`). `global_variable_commands.cpp` switches to it. This
closes finding 3 for project variables as a side effect; it is in scope
because an object-variable delete guard cannot be correct without it, and a
second half-correct walker is the worse outcome.

Its contract covers exactly four reference kinds:

```text
LogicVariableReference          (block property values)
NumberVariableExpression        (recursively)
TextComponent.bindKey
GaugeComponent.bindKey
```

*Recursively* is the load-bearing word: an expression node can sit under
unary, binary, clamp, lerp and random-range operators, and inside either
component of a `LogicVec2Value`. Inspecting the root node, or only `.x` / `.y`,
finds nothing in the cases that matter.

The AST recursion is written **once**: reuse a `logic-core` visitor if one
fits, otherwise add one small shared traversal there and let count, rename,
type compatibility and future diagnostics all consume it. Pure functions —
no `VariableReferenceManager`, no cache, no registry.

**Overrides are not references.** `localVariableOverrides` is state dependent
on a definition, not a use of it, so it never blocks a delete. It is renamed
with the key, removed on delete, removed on an incompatible type change, and
captured for undo in each case.

**Expressions pin the type to Number.** A `NumberVariableExpression` node can
only mean a Number, so a single expression node referencing the key blocks
`Number → Boolean` and `Number → String`. This falls out of the shared
type-compatibility query and applies to both variable families.

**Persistence.** Read and write `objectTypes[].localVariables` and
`scenes[].instances[].localVariableOverrides` in `project_io.cpp`, matching the
key names and value encoding the runtime already parses
(`entity-json.cpp:262-278`). The keys belong to the contract introduced with
format v10; the editor's current schema is **11**
(`project_io.cpp:50`, `project-current-format.h:11`) and is **not**
incremented — the canonical validator does not reject the keys, and editors
that ignored them wrote valid documents. Overrides whose key is not defined on
the type are dropped on read, exactly as the runtime does.

**Redo trap.** Per the known pattern in this codebase, any new validation added
to `apply()` must be gated on the captured state (`captured_` / `removed_` and
similar), otherwise redo re-runs a guard against a document that the command
itself already changed and fails. Every command below carries an
apply → undo → redo test for this reason.

### A2 — Inspector section

One section, never two. Header badge `OBJECT TYPE`, because the definition is
type-owned even when an instance is selected:

```text
OBJECT VARIABLES                          OBJECT TYPE
Collected                Boolean
  Object Type default    false
  Instance override      true      [Reset]
Health                   Number
  Object Type default    100
```

- The override row appears only when an instance is selected, and reads
  `—` (with Reset disabled) when no override is set.
- Editing the default row emits a definition Intent; editing the override row
  emits an override Intent. Neither writes the other's field. The value shown in
  the default row is the definition's, never the resolved one — the resolution
  belongs to the override row's presence.
- Empty state: `No object variables — [+ Add Object Variable]`.
- Definition editing (add, rename, retype, description, delete) is disabled
  during Play, like every other document mutation; override rows are disabled
  too, since they are authored initial values, not live state.

### Reachability — decided

**Slice A ships the limited contract: object variables are authored through a
selected instance of the type.** No `selectedObjectTypeId`, no Inspector
`ObjectType` mode, and — explicitly — **the Inspector must not read
`EditorState.logicBoardEditor`**. Panel-to-panel synchronisation through another
panel's view state is forbidden here; the Inspector's subject stays the existing
selection authority.

Two guardrails make the limitation honest rather than silent:

1. Slice A does **not** ship any `Manage Object Variables…` navigation. A
   command that cannot always reach its destination is worse than no command.
2. When the Logic Board is showing a type with no instance in the active scene,
   its variables affordance states the fact — "Object variables for Coin are
   edited by selecting a Coin instance" — instead of offering dead navigation.

Residual limitation, accepted and recorded: an Object Type whose instances live
only in another scene cannot have its variables edited from the current one.
The dead end that matters — needing a variable while authoring a rule — is
closed by Slice B's contextual creation, which executes the A1 Command directly
and does not depend on any Inspector surface existing.

The alternative (Object Type as a first-class inspectable subject) is
semantically better and remains the intended end state, but it requires an
explicit selection subject, reconciliation on delete/replace/scene switch, and a
defined transition between Object Type and Entity subjects. That is its own ADR,
not a rider on this one.

### Buffer lifecycle

Per editable field (definition key, initial value, description, override value),
following the ADR-0020 draft precedent:

| Event | Behaviour |
|---|---|
| input | draft only, no Command, no document read-back |
| Enter | validate → Intent → Command; invalid keeps focus and shows the error |
| blur | same as Enter; invalid **reverts** to the document value |
| Escape | discard draft, restore document value |
| selection change | discard draft (no implicit commit) |
| external Undo/Redo | discard draft, repaint from document |
| variable deleted / instance deleted / Replace Project / Start Play | discard draft, repaint |

A dirty buffer is never overwritten by a routine Inspector refresh. Drafts live
in the controller; only a valid commit produces an Intent.

## Non-goals

Contextual picker and create-and-assign macro command (Slice B) · new Project
surface and relocation of the Project Variables table (Slice C) · Scene
Variables · runtime `variable-manager` changes · runtime evaluation in the
editor · Inspector redesign · compact Project Variables list · Find References
navigation UI (the *counter* is in scope, the navigation is not) ·
`formatVersion` bump.

## Definition of Done

- Per Command: apply / undo / redo, no-op, and failed-apply-without-mutation.
- Invariants 1–6 covered, including override-of-undefined-key rejection and
  non-finite number rejection.
- Rename: property references, expression nodes, Text/Gauge bindings, and
  override map keys all rewritten — one test asserting the override map
  specifically.
- Type change: blocked away from Number while any expression node references
  the key; incompatible overrides dropped and restored by undo.
- Delete: blocked while referenced (including an expression-only reference —
  the regression test for finding 3), overrides removed and restored by undo.
- Persistence round-trip: definitions and overrides survive save → load; a
  project carrying them is not silently stripped; unknown override keys dropped.
- Materialisation: instance override wins over the type default; absent override
  falls back to the definition.
- Suites: `editor-core-test`, `logic-board-editor-test`, runtime `ctest`.
- `tests/reference/visual-fixture.artcade` re-baselined if the writer's output
  changes for fixtures that carry these fields.

## Implementation status

- **Slice 0 — done** (`e9f3050`): the Logic Board header button reads
  `Project Variables (N)`. Label only; the tooltip and drawer title already
  said Project Variables. No second Object Variables button, no
  `Manage Object Variables…`, no combined count.
- **A1.0 — done.** `model/variable_references.*` is the single walk;
  `forEachNumberVariableExpression` in `core/logic-number-expression.*` is the
  single AST recursion. Both `global_variable_commands.cpp` and
  `local_variable_commands.cpp` migrated — the object-scope commands landed
  with their own presentation-only walk just before this slice, and leaving it
  would have kept exactly the two walkers this decision exists to prevent.
- **A1.1 — not started.** Two defects to fix there, found while migrating:
  `RemoveObjectTypeLocalVariableCommand` and
  `SetObjectTypeLocalVariableTypeCommand` both erase instance overrides in
  `apply()` and neither restores them in `undo()`. Not yet observable, because
  the editor still does not persist overrides (finding 2).
- **A2 — not started**; blocked on A1.
- The `Create compatible variable` button in
  `logic_property_editor.cpp:401` still routes to the Project Variables
  drawer. It becomes the typed contextual creation of Slice B; Slice 0
  deliberately left it alone.

## Consequences

- The editor stops destroying `localVariables` / `localVariableOverrides` on
  save — a silent data-loss path closes.
- Project variables gain correct reference counting and rename over number
  expressions; a previously accepted Delete now fails with a message. This is a
  behaviour change on an existing feature and belongs in the commit description.
- Object variables become authorable in the common flow (instance selected)
  while Object Types without instances stay unreachable until a later ADR.
- One reference-walk module becomes the single authority on "what counts as a
  variable reference", so a fifth reference kind is added in one place.
