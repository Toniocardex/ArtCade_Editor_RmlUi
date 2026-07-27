# ADR-0032 — Contextual Project Variable Creation in the Logic Board

**Status:** Accepted
**Date:** 2026-07-27
**Scope:** ADR-0031 Slice B: typed Project Variable picker and atomic
create-and-assign flow for Logic Board state blocks
**Related:** [ADR-0031](ADR-0031-object-variables-authoring.md) (variable
ownership, shared reference walk, and the original Slice B commitment),
[ADR-0017](ADR-0017-coordinator-document-core-boundaries.md) (controller and
Command boundaries), [ADR-0027](ADR-0027-ui-design-tokens.md) (shared UI
tokens), [ADR-0029](ADR-0029-expression-text-authoring.md) (in-flow Logic Board
authoring and RmlUi dispatch constraints), Architecture Constitution,
Engineering Gates §§4, 5, 12, 18, 24

## Context

The state blocks in the Logic Board require a typed Project Variable:

| Block | Required type |
|---|---|
| Set Number | Number |
| Add to Number | Number |
| Subtract from Number | Number |
| Compare Number | Number |
| Toggle Boolean | Boolean |

`logic-core` declares their variable property as
`LogicPropertySemantic::GlobalVariable`, describes the blocks as operating on
a project variable, and exposes the required type through
`Logic::requiredVariableType(block.typeId)`. This is runtime meaning, not an
editor label.

The current property editor filters `ProjectDoc.globalVariables` by that type.
If at least one compatible variable exists it renders a normal dropdown. If
none exists, `logic_property_editor.cpp` renders:

```text
[ Create compatible variable ]
```

That button dispatches `toggle-global-variables`, which opens the Project
Variables drawer at the top of the Logic Board. The author must then:

1. leave the rule property;
2. create an untyped default Number variable in the drawer;
3. rename and, for Toggle, retype it;
4. return to the rule;
5. select it.

The originating property address and required type are lost. Creation and
assignment become separate Commands and separate Undo entries, and Toggle
Boolean initially creates the wrong type.

This is the dead end recorded as Slice B in ADR-0031. ADR-0031 intentionally
did not specify the solution; this ADR is that missing decision.

### Scope clarification: Project, not Object

Slice B does **not** make state blocks operate on Object Variables.
`LogicVariableReference` in these blocks is a project-scope reference and the
runtime bindings are `state.*` operations over the global variable manager.
Object Variables remain available to Number Expressions through local-scope
expression nodes, as decided by ADR-0028/0029/0031.

Adding a scope arm to state-block references would require a logic schema and
runtime execution decision. It cannot be smuggled into a picker change.

## Decision

### A typed picker remains at the property

Every `GlobalVariable` property renders one in-flow typed picker:

```text
Variable   [ score                         v ]
             score
             highScore
             ───────────────────────────────
             + Create Number Project Variable…
```

For Toggle Boolean, the final entry reads
`+ Create Boolean Project Variable…`.

The picker:

- lists only existing Project Variables whose type equals
  `Logic::requiredVariableType(block.typeId)`;
- preserves their document order;
- marks the currently assigned variable;
- keeps the create entry available even when compatible variables already
  exist;
- never lists Object Variables;
- is disabled during Play.

When there are no compatible variables, the closed trigger says
`Create Number Project Variable…` or `Create Boolean Project Variable…`
instead of the untyped `Create compatible variable`.

The dropdown stays in flow. ADR-0029 already established that the Logic Board's
scroll container clips popup children; this slice does not reintroduce an
absolute popup.

### Contextual creation is a panel-local draft

Choosing the create entry replaces the picker body with a compact in-flow
creator at the same property:

```text
Variable   [ variable-1                 ]  Number
                                         [Create] [Cancel]
```

The type is fixed by the block descriptor and is not editable in this flow.
The initial value is the canonical default for that type:

- Number → `0.0`;
- Boolean → `false`.

Description starts empty. The suggested key is the first free
`variable-N`, matching the existing Project Variables creation convention.

The draft is owned by `LogicBoardPanel` and addressed by:

```text
ObjectTypeId
LogicRuleId
LogicPropertyTarget
block index
property key
required variable type
```

Typing mutates only this panel-local draft. It creates no Command, revision,
dirty state, or Undo entry.

| Event | Behaviour |
|---|---|
| input | update draft only; clear the previous inline error |
| Enter / Create | validate and execute the atomic Command |
| invalid Enter / Create | retain exact text, show inline error, keep focus |
| Escape / Cancel | discard draft and restore the typed picker |
| blur | no implicit create and no implicit cancel |
| rule/block/Object Type change | discard draft |
| external Undo/Redo | discard draft and repaint |
| Replace Project / Start Play | discard draft and repaint |

Blur is deliberately inert: clicking `Create` moves focus before its click is
dispatched, and a blur-cancels policy would destroy the button in the same
frame. Creation remains explicit.

No `EditorState` or `EditorUiState` field is added. The draft has the same
panel-local lifetime as the Logic Board's expression and key-capture state.

### One dedicated atomic Command

Confirmation executes one new authoring Command:

```text
CreateAndAssignGlobalVariableCommand
    property address
    GameVariableDefinition
```

No new Intent is required. The UI action is already resolved to a complete
domain mutation, and the existing Logic Board controller executes authoring
Commands directly. The controller may validate presentation-level completeness
to provide immediate feedback; the Command repeats every domain validation.

The Command stages a single `ProjectDoc`:

1. resolve the Object Type, Logic Board, rule, block, and property;
2. verify that the descriptor declares a `GlobalVariable` property;
3. derive the required type from the block descriptor;
4. verify that the submitted definition has exactly that type;
5. validate the new Project Variable key/value with the canonical global
   variable validator;
6. reject a duplicate key;
7. capture the property's exact previous `LogicValue` on first apply only;
8. append the definition to `globalVariables`;
9. assign `LogicVariableReference{definition.key}` to the property;
10. structurally validate the resulting board and commit the staged document
    once.

Failure at any step commits nothing.

The Command is a purpose-built atomic Command, not a generic Composite Command
framework. Both mutations live in the same `ProjectDocument`, require one
staged validation boundary, and have one semantic name. Introducing generic
composition would add rollback and ordering machinery without another caller.

### Undo and Redo

One confirmation creates one history entry.

Undo stages the reverse as one document commit:

1. restore the property's exact captured `LogicValue`;
2. remove the created Project Variable;
3. validate and commit once.

Redo reapplies the captured definition and assignment without re-reading UI
state or recapturing the previous value.

The restored value may be empty or may name a previously unresolved variable:
Undo restores the exact pre-command authoring state rather than inventing a
different default.

Normal selection of an already-existing variable remains
`SetLogicPropertyCommand` and remains one independent Undo entry.

### Authority and mutation map

| Concern | Owner | Mutation |
|---|---|---|
| Project Variable definition | `ProjectDocument.globalVariables` | `CreateAndAssignGlobalVariableCommand` |
| Logic property reference | owning Object Type's `LogicBoardDef` in `ProjectDocument` | same Command |
| Required variable type | `logic-core` block descriptor / `requiredVariableType` | derived, never stored in UI |
| Contextual name/error/open state | `LogicBoardPanel` draft | presentation only |
| Command execution/history/Play gate | `EditorCoordinator` | existing orchestration |

RmlUi only emits semantic actions and renders projection state. It never
mutates either collection.

### Invalidation and domain change

The atomic Command invalidates the union already required by a Project Variable
creation and a Logic property assignment:

```text
LogicBoard | Inspector | Viewport
```

It reports `DomainChange::projectChanged()`. The change crosses a global
definition and an Object-Type-owned board, and the current `DomainChange`
contract carries one change kind rather than a set; the project-wide kind is
the honest existing representation.

Toolbar invalidation continues to be added by `EditorCoordinator` when the
history entry is recorded.

### Edit, Play, persistence, and migration

- Edit mode is the only mode that exposes the create entry.
- Starting Play discards an open contextual draft.
- The Command remains blocked by the Coordinator's Play gate even if invoked
  through a stale element.
- Play materializes the committed document normally; there is no live runtime
  mutation or reverse-sync.
- Both fields already exist in the current project and Logic Board formats.
  No schema, project-format, serializer, or migration change is required.

## Failure paths and invariants

The feature must preserve:

1. A created variable's type exactly matches the target block's required type.
2. The key is valid and unique among Project Variables.
3. The target still resolves to the same Object Type/rule/block/property and
   still has `GlobalVariable` semantics at apply time.
4. Variable creation and property assignment either both commit or neither
   commits.
5. Failed apply, failed undo, and failed redo do not mutate revision, dirty
   state, or history position.
6. A panel refresh cannot retarget a draft to another property.
7. Object Variables never appear in this picker, even when their key and type
   match.
8. User-authored keys are escaped before entering Rml markup.

## Alternatives rejected

### Keep opening the Project Variables drawer

Rejected. It loses the property address and required type, requires several
manual steps, and cannot make create-and-assign one Undo operation.

### Execute AddGlobalVariableCommand, then SetLogicPropertyCommand

Rejected. Two Commands expose a partially completed state, create two history
entries, and require compensating rollback if assignment fails.

### Add a generic CompositeCommand framework

Rejected for this slice. A dedicated staged Command is smaller and protects the
actual invariant. Revisit generic composition only after a second concrete
multi-Command workflow demonstrates shared requirements.

### Include Object Variables in the same picker

Rejected. These state blocks have project-scope runtime semantics. Supporting a
local scope requires a separate runtime/schema decision.

### Auto-create immediately with no name step

Rejected. It would leave generic `variable-N` definitions throughout the
project and make accidental activation an authoring mutation. The deterministic
name is a suggestion, not an implicit commit.

## Non-goals

- Object Variable references in `state.*` blocks;
- contextual creation inside Number Expression autocomplete;
- moving or redesigning the Project Variables drawer (ADR-0031 Slice C);
- a generic Command composition framework;
- Project Variable type editing from the contextual creator;
- description or initial-value editing in the contextual creator;
- Find References navigation;
- runtime variable-manager changes;
- schema or project-format changes.

The created definition remains fully editable in the existing Project Variables
drawer after creation.

## Verification

### Command tests

- Number create + assign: apply → undo → redo;
- Boolean create + assign: apply → undo → redo;
- one confirmation increases history size by exactly one;
- Undo restores the exact previous property value and removes the definition;
- duplicate/invalid/empty key fails without mutation;
- mismatched submitted type fails without mutation;
- stale Object Type, rule, block index, property key, or changed block type
  fails without mutation;
- non-`GlobalVariable` property target fails without mutation;
- redo does not recapture state;
- a new Command after Undo clears Redo through the existing history contract.

### Projection and routing tests

A real RmlUi test starts at the rendered element and crosses the single
`EditorUi` listener:

- Number blocks list Number Project Variables and exclude Boolean/Object
  Variables;
- Toggle lists Boolean Project Variables and excludes Number/Object Variables;
- the create entry remains present when compatible variables exist;
- opening creation and typing do not mutate the document;
- Escape/Cancel restores the picker;
- invalid confirmation retains text, error, and focus;
- valid Enter and valid Create each create and assign;
- one Undo removes the variable and restores the old selection;
- switching rule/block/Object Type discards the draft;
- Play disables the affordance and cannot mutate;
- the picker remains open for the complete frame that opened it.

### Existing suites

- `editor-core-test`;
- `logic-board-editor-test`;
- the real RmlUi Logic Board routing suite;
- `ui-stylesheet-tokens-test`;
- runtime `ctest` for unchanged state-block execution.

## Implementation status

Implemented on 2026-07-27.

- `CreateAndAssignGlobalVariableCommand` stages creation and assignment as one
  `ProjectDocument` mutation, publishes one history entry, and restores the
  exact previous `LogicValue` on Undo.
- Every `GlobalVariable` property now renders the typed Project Variable picker
  and its always-available contextual create entry. Object Variables and
  incompatible Project Variables are excluded.
- `LogicBoardPanel` owns the contextual name/error/focus draft. The controller
  derives the required type from the block descriptor and routes confirmation
  through the atomic Command; `EditorUi` handles Enter and Escape after RmlUi
  dispatch so replacing the field cannot invalidate the active event target.
- Draft discard is wired for Object Type/tab changes, collapse, external
  Undo/Redo, Replace Project, and Start Play. Blur remains inert.
- Core command tests cover Number and Boolean apply/undo/redo plus atomic
  failure paths, stale addresses, semantic/type mismatch, and the Play gate.
  The real RmlUi routing suite covers typed projection, contextual creation,
  validation/focus retention, Enter/Create, Escape/Cancel, one-step Undo/Redo,
  discard transitions, Replace Project, and Play.
- No runtime, schema, serializer, project-format, or migration change was
  required.

## Definition of Done

- authority, type derivation, atomicity, Undo/Redo, Play, failure paths, and
  tests above are implemented;
- no new UI stack, manager, event bus, mutable document exposure, or generic
  composite infrastructure is introduced;
- the old `Create compatible variable` drawer detour is gone from every
  `GlobalVariable` property;
- implementation status is recorded here before the ADR moves from Proposed to
  Accepted.
