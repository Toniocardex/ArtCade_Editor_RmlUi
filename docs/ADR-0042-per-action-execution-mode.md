# ADR-0042 — Per-Action Execution Mode

**Status:** Accepted
**Date:** 2026-07-29
**Supersedes:** [ADR-0041](ADR-0041-logic-board-action-groups.md)

## Context

Action Groups added branches, branch-local conditions and a second structural
level to solve a smaller need: actions in the same rule may require different
execution modes. That structure made the model, UI, commands, migration and
runtime state harder to reason about.

## Decision

`ProjectDocument` remains the authoring authority. A rule owns one trigger, one
shared condition list and one flat, authored-order action list:

```cpp
struct LogicActionDef {
    LogicActionId id;
    LogicExecutionMode executionMode =
        LogicExecutionMode::EveryOccurrence;
    LogicBlockDef block;
};

struct LogicRuleDef {
    LogicRuleId id;
    std::string name;
    bool enabled = true;
    std::string sectionId;
    LogicBlockDef trigger;
    std::vector<LogicConditionClause> conditions;
    std::vector<LogicActionDef> actions;
};
```

Action IDs are non-empty and unique within a rule. Reorder and block updates
preserve the ID. Copying an action allocates a fresh ID. Empty action lists are
valid while authoring.

## Commands and Undo

Action mutations are atomic Commands addressed by
`objectTypeId + boardId + ruleId + actionId`. The minimum command surface is:

- add action;
- remove action;
- move action;
- update action block or one of its typed properties;
- set action execution mode.

The board ID protects queued UI edits from mutating a newly replaced board.
Each successful mutation creates one Undo entry; a no-op creates none. Play
rejects authoring Commands.

## Runtime

The trigger and shared conditions are evaluated once per occurrence. Actions
are visited in authored order. Immediately before each block, generated Lua
asks the existing runtime execution-state service whether the action may run.
The stable state key is:

```text
boardId:ruleId:actionId
```

`EveryOccurrence` runs whenever the shared predicate is eligible.
`OncePerActivation` runs on the first eligible occurrence and rearms after the
shared activation becomes false. No branch state, group snapshot, additional
manager or alternate runtime authority is introduced.

Wait sequencing and failure behavior are unchanged: later actions remain in
authored order after a Wait continuation, and an action failure still aborts
the remaining actions for that dispatch.

## Persistence and migration

Logic Board schema version 6 serializes each action as `id`,
`executionMode`, and `block`.

- Schemas 3 and 4: the rule-level execution mode is copied to every migrated
  action and deterministic `action-N` IDs are assigned.
- Schema 5 Action Groups: branches are flattened in authored order, each
  branch mode is copied to its actions, and deterministic `action-N` IDs are
  assigned.
- A schema 5 branch with local conditions is rejected with an explicit
  migration error. Silently discarding or changing those conditions is not
  allowed.

Canonical saves contain `actions` only; `branches` and rule-level
`executionMode` are not emitted.

## UI

THEN renders one flat action card per action. Each card has a compact `Run`
selector with:

- Every occurrence;
- Once per activation.

The WHEN column has no execution-mode control. The UI contains no Action Group,
THEN Group, group condition or Add Action Group affordance.

## Verification / DoD

- native editor build succeeds;
- schema 3/4 and schema 5 migration are covered;
- schema 5 local-condition migration fails explicitly;
- independent per-action gates are covered in Play;
- gate keys use stable IDs and survive reorder;
- copied actions receive a fresh ID and therefore a fresh gate;
- stale-board action Commands are rejected;
- JSON round-trip, Undo/Redo and typed property edits are covered;
- no production Action Group model, command, UI or codegen path remains.
