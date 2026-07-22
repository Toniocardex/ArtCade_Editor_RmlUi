# ADR-0006 — Logic Board rule-card duplication

**Status:** Accepted
**Date:** 2026-07-22
**Scope:** native RmlUi Logic Board authoring

## Context

A Logic Board rule is presented as one card: **WHEN**, optional **IF** clauses,
and ordered **THEN** actions. Recreating a near-identical rule currently means
re-entering its trigger, conditions, properties, execution mode, and actions.
That is slow and makes small variations (for example, one rule per movement
key) unnecessarily error-prone.

The card header already contains the immediate rule-level operations: enable,
move up, move down, and delete. It is therefore the established scope and
location for a rule-level clone. Duplicating one block, one condition, or the
entire board is a different operation and must not be implied by this feature.

## Decision

Add an always-visible **Clone rule** icon button to every Logic rule-card
header. Its position is between the enabled toggle and the ordering controls:

```text
[On/Off] [Clone rule] [Move up] [Move down] [Delete rule]
```

The button uses the same compact icon-button treatment as the arrows, has the
tooltip and accessible name **Clone rule**, and is disabled in Play exactly as
the other authoring controls are. It is not hidden in an overflow menu and it
does not require selecting the card first.

One click immediately creates a new card **directly after** the source card.
There is no confirmation dialog: the operation is safe, obvious in the list,
and one Undo reverses it. After reconciliation, the editor keeps the current
scroll position unless the new card header is outside the visible Logic Board
area; only in that case it scrolls just enough to reveal the clone. The clone
uses the normal expanded/collapsed presentation for a newly created card. No
toast, modal, or artificial selection state is needed: the adjacent new card
is the acknowledgement.

### What is copied

The atomic unit is the complete `LogicRuleDef`. The clone deep-copies:

- `enabled`, `executionMode`, and `sectionId`;
- the trigger and all of its properties;
- every condition, including its join/negation and properties, in order;
- every action and its properties, in order.

It receives a newly allocated, unique `LogicRuleId`; a rule ID is never copied
or reused. Its authoring name is derived deterministically from the source:
`"<source name> Copy"`, or the first available numbered variant such as
`"<source name> Copy 2"`. The name is only an authoring label and does not
alter compiled/runtime behaviour. The Logic Board ID, schema/API versions, and
all other rules remain unchanged.

### Authority, Intent, and Command

`ProjectDocument` remains the sole authority for persisted Logic Board data.
RmlUi only renders the button and sends stable identifiers; it neither clones
data nor allocates IDs.

```text
RmlUi action "duplicate-logic-rule" (objectTypeId, sourceRuleId)
    -> DuplicateLogicRuleIntent
    -> EditorCoordinator
    -> DuplicateLogicRuleCommand
    -> ProjectDocument.replaceLogicBoard
```

The Coordinator reads the current authoritative board, resolves the source,
allocates the new rule ID/name, and constructs the immutable clone payload and
insertion index. `DuplicateLogicRuleCommand` stages a copied board, inserts
that payload at `sourceIndex + 1`, validates the complete board with the
existing authoring validator, then commits through the existing Logic Board
replacement path. It emits the usual `LogicBoard` invalidation and targeted
`DomainChange::logicBoardChanged(objectTypeId)`.

The command stores the before-board snapshot. Thus clone, Undo, and Redo are
one deterministic history entry and restore the exact rule order and IDs. The
operation must not be implemented as several Add/Set commands or a Composite
Command: one complete board replacement already provides the required atomic
boundary.

### Invariants and failure path

- The source Object Type and its Logic Board must still exist.
- The source rule must still exist at command application time.
- The generated ID must be non-empty and unique in that board.
- The resulting board must pass the existing authoring validation; otherwise
  no document mutation, revision/dirty change, invalidation, or history entry
  is produced.
- The command never shares mutable state with the source: subsequent edits to
  either rule affect only that rule.

If an action arrives with stale or invalid IDs, the Coordinator/Command rejects
it through the normal error path and leaves the UI and document unchanged.
The transient pending-scroll target is panel-local, is cleared on board/object
switch, Play, and panel teardown, and is never saved or placed in Undo history.

## Edit, Play, persistence, and lifecycle

Clone is an Edit-only authoring operation. During Play the header control is
disabled and the controller rejects the action before an Intent/Command is
executed. A running session retains its immutable runtime snapshot; cloning
never changes the active world and is only visible after a later Play start.

No project-schema or runtime change is required: `LogicRuleDef` already owns
the data and existing Logic Board serialization persists the inserted rule.
The operation changes project revision and dirty state exactly once on success.
Replace Project/board retargeting discards the panel-local pending scroll ID;
the document never retains UI state.

## Non-goals

- duplicating individual triggers, conditions, actions, or property fields;
- cloning multiple selected cards, clipboard/cross-board paste, or linked
  template/instance rules;
- changing rule execution semantics, compiler output, Logic catalog, or the
  runtime;
- adding a new manager, cache, event bus, persistent selection model, or
  migration solely for this control.

## Alternatives rejected

1. **Duplicate from a context/overflow menu.** Rejected: cloning is a common
   structural authoring operation and would be slower and less discoverable
   than the existing header controls.
2. **Append the clone at the end of the board.** Rejected: it breaks the visual
   relationship to the source and can change intended rule ordering.
3. **Create an empty rule, then copy fields through separate commands.**
   Rejected: it exposes intermediate invalid states and makes Undo surprising.
4. **Reuse the source ID or create a linked duplicate.** Rejected: rule IDs are
   identity, while a clone must be independently editable and serializable.
5. **Ask for confirmation.** Rejected: the action is fully reversible and an
   extra dialog harms the immediate workflow.

## Implementation slice and verification

The slice adds only the narrow pieces required by the decision:

- `DuplicateLogicRuleIntent`, its `EditorCoordinator::apply` overload, and
  `DuplicateLogicRuleCommand` alongside the existing Logic Board commands;
- the `ProjectDocument` friendship required by that command, without a new
  document mutation API;
- the header icon/action, Play guard, local reveal-after-reconciliation
  behaviour, and matching RCSS tooltip/icon styling if needed;
- focused tests in `editor_core_test`.

Required tests:

1. cloning inserts one rule immediately after its source, with a unique ID and
   deterministic copied name;
2. every persisted rule field (trigger, conditions, joins, negations, actions,
   properties, mode, enabled, and section) is equal by value, while changing
   the clone later does not change the source;
3. Undo restores the exact pre-clone board and Redo restores the same clone and
   order, with one history entry and the normal Logic Board domain change;
4. missing board/source, duplicate/invalid ID, and validation failure leave the
   document, revision, dirty state, and history unchanged;
5. save/load preserves both independently addressable rules;
6. the RmlUi action renders its accessible tooltip, is available in Edit,
   disabled/rejected in Play, and produces no runtime-world or authoring
   mutation while Play is active;
7. a clone below the viewport is revealed after reconciliation, while a clone
   already visible does not cause a scroll jump.
