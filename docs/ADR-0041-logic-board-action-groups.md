# ADR-0041 — Logic Board Action Groups

**Status:** Proposed — pending review
**Date:** 2026-07-28
**Scope:** Logic Board persistent model, JSON migration, validation, generated Lua, runtime execution-state keys, editor Commands/Intents, and the native Logic Board UI.
**Related:** Constitution ownership/command invariants; ADR-0013 validation purposes; existing `LogicExecutionMode` behavior and generated-Lua runtime contract.

---

## Context

A Logic Board rule currently owns one trigger, one shared condition list, one
`LogicExecutionMode`, and one flat action list:

```cpp
struct LogicRuleDef {
    LogicRuleId id;
    std::string name;
    bool enabled = true;
    LogicExecutionMode executionMode =
        LogicExecutionMode::EveryOccurrence;
    std::string sectionId;
    LogicBlockDef trigger;
    std::vector<LogicConditionClause> conditions;
    std::vector<LogicBlockDef> actions;
};
```

This means every action in a rule must share the same execution mode.

A real authoring case exposed the limitation:

```text
WHEN
  On Collision Enter
  Other Type = Coin

THEN — Every occurrence
  Jump
  Add to Number
  Camera Shake

THEN — Once per activation
  Play Sound
```

Today the author must duplicate the complete rule only to assign a different
execution mode to `Play Sound`. The duplicated rule repeats:

- the same trigger;
- the same `Other Type` filter;
- the same event context;
- any shared conditions;
- future maintenance changes.

The existing `Once per activation` behavior is correct for the reported use
case and must be preserved exactly. This ADR does not redefine that behavior.
It only moves the execution gate from the whole rule to an ordered action
group within the rule.

The problem is therefore not missing action-level throttling and not missing
nested event subscriptions. It is the absence of multiple independently gated
`THEN` branches under one authoritative event definition.

---

## Decision

Introduce **Logic Action Branches** in the persistent domain model.

The editor-facing label will be **Action Group** or **THEN Group**. The domain
type remains `LogicActionBranchDef`.

A rule owns:

- one trigger;
- zero or more shared conditions;
- one or more ordered action branches.

Each branch owns:

- a stable branch ID;
- its own `LogicExecutionMode`;
- zero or more branch-local conditions;
- zero or more ordered actions.

```cpp
using LogicActionBranchId = std::string;

struct LogicActionBranchDef {
    LogicActionBranchId id;

    LogicExecutionMode executionMode =
        LogicExecutionMode::EveryOccurrence;

    // Evaluated in addition to LogicRuleDef::conditions.
    std::vector<LogicConditionClause> conditions;

    // Authored order is runtime execution order inside this branch.
    std::vector<LogicBlockDef> actions;
};

struct LogicRuleDef {
    LogicRuleId id;
    std::string name;
    bool enabled = true;
    std::string sectionId;

    LogicBlockDef trigger;

    // Shared by every branch in authored order.
    std::vector<LogicConditionClause> conditions;

    // Single ordered authority for the rule's THEN behavior.
    std::vector<LogicActionBranchDef> branches;
};
```

The following fields are removed from `LogicRuleDef` after migration:

```cpp
LogicExecutionMode executionMode;
std::vector<LogicBlockDef> actions;
```

No parallel legacy representation remains in memory or in newly serialized
boards.

---

## Terminology

### Domain terminology

- `LogicActionBranchDef`
- `LogicActionBranchId`
- `branches`

### Editor terminology

- **Action Group**
- **THEN Group**
- **Add Action Group**

The UI must not call this feature a sub-event. A branch does not install,
subscribe to, or dispatch a second trigger.

---

## Authoring semantics

### Shared predicate

The rule-level predicate is:

```text
trigger occurrence
AND all shared rule conditions
```

### Effective branch predicate

For each branch:

```text
effective branch predicate =
    trigger occurrence
    AND all shared rule conditions
    AND all branch-local conditions
```

A branch with no local conditions inherits only the shared predicate.

### No nested triggers

A branch may contain conditions and actions, but never another trigger.

The following are explicitly out of scope:

- child events;
- nested subscriptions;
- event trees;
- parallel branches;
- asynchronous branch execution.

---

## Dispatch semantics

For each trigger dispatch:

1. Acquire the trigger context once.
2. Evaluate all shared rule conditions.
3. Evaluate every branch's local conditions.
4. Compute every branch's execution-mode eligibility.
5. Execute eligible branches in authored order.
6. Execute each branch's actions in authored order.

All branch predicates and execution gates must be evaluated before the first
branch action mutates runtime state.

Conceptually:

```lua
local _sharedEligible = <shared conditions>

local _branchAEligible =
    _sharedEligible and <branch A local conditions>

local _branchBEligible =
    _sharedEligible and <branch B local conditions>

local _branchARuns =
    <existing execution-mode gate for branch A>

local _branchBRuns =
    <existing execution-mode gate for branch B>

if _branchARuns then
    -- branch A actions
end

if _branchBRuns then
    -- branch B actions
end
```

This prevents an earlier branch from changing a variable and thereby changing
the eligibility of a later branch during the same dispatch.

Authors who need to react to the result of an earlier mutation must use a
separate rule/event that observes the resulting state.

---

## Execution-mode compatibility

### Existing behavior is authoritative

This ADR does not redefine:

- `EveryOccurrence`;
- `OncePerActivation`;
- when activation begins or ends;
- when gate state is written;
- how gate state resets;
- how action failure interacts with the gate.

Before changing the model, add characterization tests that freeze the current
runtime behavior of both execution modes.

The branch implementation must reuse the existing gate logic. The only
identity change is:

```text
before:
    boardId + ruleId

after:
    boardId + ruleId + branchId
```

Each branch therefore has independent execution state.

### Diagnostics

`LB_EXECUTION_MODE_PULSE_REDUNDANT` must be removed or narrowed so it is emitted
only where equivalence is actually proven.

It must not warn that `Once per activation` is redundant in a case where the
current runtime demonstrably produces different, intended behavior.

---

## Runtime state identity

A branch runtime key must be stable across:

- save/load;
- generated-Lua regeneration;
- branch reorder;
- action reorder;
- editor restart.

Recommended identity:

```text
boardId : ruleId : branchId
```

The index of a branch is never part of its persistent identity.

Reordering branches changes execution order but not branch identity or gate
state.

---

## Error semantics

This ADR preserves the current rule-dispatch failure policy.

If an action raises a runtime error:

- the existing diagnostic path is used;
- execution does not silently continue into later branches unless that is
  already the current rule behavior;
- branch isolation is not introduced in this slice.

The implementation must not change when the `Once per activation` gate is
committed relative to action execution. That timing remains whatever the
current characterization tests establish.

---

## Ordering

`LogicRuleDef::branches` is the single authority for branch order.

Runtime order is:

```text
rule order
→ branch order
→ action order
```

All three are authored vector order.

No map, UI DOM order, generated key sort, or runtime registration order may
become a competing authority.

---

## Structural invariants

A valid rule must satisfy:

- at least one action branch;
- every branch ID is valid;
- branch IDs are unique within the rule;
- branch order is represented only by the vector;
- actions belong to exactly one branch;
- local conditions belong to exactly one branch;
- the rule no longer stores legacy `executionMode` or `actions`.

Recommended initial branch limit:

```cpp
constexpr std::size_t kMaxLogicActionBranchesPerRule = 8;
```

Existing total limits must continue to apply across the complete rule:

```text
total actions =
    sum(branch.actions.size())

total conditions =
    rule.conditions.size()
    + sum(branch.conditions.size())
```

Adding branches must not multiply existing action or condition limits.

---

## Validation policy

### StructuralCommit

Reject:

- zero branches;
- invalid branch IDs;
- duplicate branch IDs;
- branch-count overflow;
- total action-count overflow;
- total condition-count overflow;
- malformed branch/action/condition structures.

### AuthoringDiagnostics

Warn, but remain editable and saveable:

- empty branch;
- branch with conditions but no actions;
- suspicious execution-mode combinations when equivalence is genuinely known.

### Executable

Validate:

- shared conditions;
- branch-local conditions;
- every branch action;
- required context against the rule trigger;
- execution-mode compatibility.

Unknown block handling remains governed by ADR-0013. Adding branches must not
make repairable boards unloadable.

Diagnostics should include:

```text
objectTypeId
boardId
ruleId
branchId
blockTypeId
propertyKey
```

where applicable.

---

## Persistence and migration

The Logic Board schema is bumped from the implementation branch's current
version:

```text
schema N
→ schema N+1
```

The ADR intentionally does not hard-code the numeric value because active
branches may not currently share the same schema constant.

### Legacy form

```json
{
  "id": "rule-collect",
  "executionMode": "everyOccurrence",
  "trigger": { "typeId": "collision.enter" },
  "conditions": [],
  "actions": [
    { "typeId": "state.add_number" }
  ]
}
```

### New form

```json
{
  "id": "rule-collect",
  "trigger": { "typeId": "collision.enter" },
  "conditions": [],
  "branches": [
    {
      "id": "branch-1",
      "executionMode": "everyOccurrence",
      "conditions": [],
      "actions": [
        { "typeId": "state.add_number" }
      ]
    }
  ]
}
```

### Migration contract

For every legacy rule:

```text
legacy rule.executionMode
legacy rule.actions

→

one migrated branch:
    id = deterministic stable ID
    executionMode = legacy executionMode
    conditions = []
    actions = legacy actions
```

Recommended deterministic migrated ID:

```text
branch-1
```

provided it is unique within the rule.

The decoder:

- reads the legacy form only as migration input;
- produces only the new in-memory representation;
- preserves unknown blocks and authored order.

The serializer:

- writes only `branches`;
- never emits legacy rule-level `executionMode` or `actions`.

After one save, the project contains only the new format.

No runtime fallback may continue reading both representations indefinitely.

---

## Editor UX

### Single-group presentation

A rule with one branch should remain visually close to the current UI:

```text
WHEN | IF | THEN                 Run: Every occurrence
            [ actions... ]
            + Add Action
```

Do not add unnecessary nesting chrome when only one group exists.

### Multiple-group presentation

When a second branch exists:

```text
THEN GROUP 1                     Run: Every occurrence
  [ actions... ]
  + Add Condition
  + Add Action

THEN GROUP 2                     Run: Once per activation
  IF
    [ local conditions... ]
  [ actions... ]
  + Add Condition
  + Add Action

+ Add Action Group
```

Each branch provides:

- execution-mode selector;
- add local condition;
- add action;
- move up/down;
- duplicate;
- delete.

The last remaining branch cannot be deleted. Alternatively, deleting it must
atomically replace it with one empty default branch. The preferred rule is:

```text
a Logic rule always owns at least one Action Group
```

### Shared conditions

The existing central `IF` column remains the shared condition surface.

Branch-local conditions are visually nested inside their Action Group and
must be labeled clearly enough that they are not confused with shared
conditions.

### Non-goals for the MVP UI

Do not add:

- custom branch names;
- `else`;
- nesting;
- drag-and-drop between rules;
- per-action execution mode;
- delays or cooldowns;
- parallel execution;
- child triggers.

---

## Command and Intent model

The UI remains presentation-only.

All mutations flow through the existing Controller → Intent/Command →
Coordinator → Command → ProjectDocument path.

Minimum required operations:

```text
AddActionBranch
RemoveActionBranch
MoveActionBranch
DuplicateActionBranch
SetActionBranchExecutionMode

AddBranchCondition
UpdateBranchCondition
RemoveBranchCondition
MoveBranchCondition

AddBranchAction
UpdateBranchAction
RemoveBranchAction
MoveBranchAction
```

Existing action and condition commands may be generalized to accept
`branchId`, provided there is one mutation path and no compatibility manager.

Every mutating command must:

- validate rule and branch identity;
- be atomic;
- support Undo/Redo;
- reject failed and no-op changes from history;
- emit the appropriate domain change/invalidation;
- preserve stable IDs during reorder.

Removing a branch removes its local conditions and actions atomically.

Duplicating a branch creates a new branch ID while preserving copied content
and authored order.

---

## Generated Lua

The compiler must:

- compile one trigger subscription per rule;
- compile shared conditions once;
- compile local branch conditions;
- precompute branch eligibility before actions;
- apply execution gates independently;
- emit branches and actions in authored order;
- include `branchId` in runtime-state keys;
- preserve existing feature negotiation;
- preserve existing runtime error behavior.

A rule with one migrated branch must generate behavior equivalent to the
pre-migration rule.

No Lua API-version bump is required unless implementation reveals that the
existing runtime helper cannot accept branch-qualified keys. Prefer changing
generated keys over adding a new public Lua surface.

---

## Context propagation

Every branch receives the same trigger context captured for the rule
dispatch.

For collision events, for example:

```text
Self
Other
collision/event data
```

are shared by all branches for that dispatch.

Branch state must not retain ephemeral event context after dispatch.

Availability validation for branch conditions/actions uses the parent rule's
trigger descriptor and provided context.

---

## Rejected alternatives

### Duplicate the complete rule

Rejected because it duplicates trigger configuration, shared conditions,
context references, diagnostics, and maintenance.

### Per-action execution mode

Rejected for this slice because multiple actions commonly share one execution
policy and one local condition set. Per-action gates would be unnecessarily
granular and would not solve conditional grouping cleanly.

### True nested sub-events

Rejected because they require new trigger subscriptions, lifecycle rules,
context inheritance, scheduling, and ordering semantics. The reported need
requires only branches under one event.

### Keep rule-level execution mode and add optional branch overrides

Rejected because it creates two execution authorities and ambiguous
inheritance.

### Keep both legacy actions and branches

Rejected because it creates duplicate persistent representations and hidden
synchronization.

---

## Consequences

### Positive

- one authoritative trigger can drive several independently gated behaviors;
- duplicated rules are eliminated;
- shared event context is configured once;
- local branch conditions become possible without nested events;
- execution order remains explicit and deterministic;
- existing boards migrate without semantic changes;
- future action-group features have a stable structural home.

### Costs

- Logic Board schema migration;
- wider Command/Intent surface;
- compiler and generated-Lua refactor;
- independent runtime gate state per branch;
- more complex card rendering and diagnostics;
- all fixtures/tests that inspect rule actions must be updated.

---

# Implementation Plan

## Phase 0 — Characterize current execution modes

Before changing the model:

1. Add runtime/compiler tests for the current `EveryOccurrence` behavior.
2. Add tests for the current `OncePerActivation` behavior using the real
   trigger that motivated the feature.
3. Freeze:
   - gate reset boundaries;
   - gate-write timing;
   - error behavior;
   - generated-Lua state keys;
   - repeat behavior across multiple trigger occurrences.
4. Record the current behavior in test names and comments.
5. Do not proceed if the observed behavior contradicts the desired authoring
   contract; resolve that separately rather than silently changing it during
   this refactor.

## Phase 1 — Core model

1. Add `LogicActionBranchId`.
2. Add `LogicActionBranchDef`.
3. Replace `LogicRuleDef::executionMode` and `LogicRuleDef::actions` with
   `LogicRuleDef::branches`.
4. Add branch-limit constants and helpers.
5. Update equality, clone, copy, and default-rule construction.
6. Ensure every newly created rule receives one default branch.
7. Update fixture builders and test factories.

Primary areas:

```text
vendor/artcade-runtime/src/core/types.h
vendor/artcade-runtime/src/modules/logic-core/include/
vendor/artcade-runtime/src/modules/logic-core/src/
```

## Phase 2 — JSON codec and migration

1. Read the current schema constant from the implementation branch.
2. Bump `N → N+1`.
3. Decode legacy rule-level `executionMode/actions`.
4. Materialize one deterministic branch.
5. Decode the new `branches` form.
6. Serialize only the new form.
7. Reject duplicate/invalid branch IDs structurally.
8. Preserve unknown blocks and authored order.
9. Add round-trip and migration tests.
10. Verify a migrated project remains stable after a second save/load.

## Phase 3 — Validation and catalog projections

1. Validate all shared conditions.
2. Validate every branch-local condition.
3. Validate every branch action.
4. Apply required-context checks using the rule trigger.
5. Apply aggregate action/condition limits.
6. Include `branchId` in diagnostics.
7. Remove or narrow `LB_EXECUTION_MODE_PULSE_REDUNDANT`.
8. Ensure disabled rules remain repairable under ADR-0013.
9. Update any editor projections that currently flatten
   `rule.executionMode/actions`.

## Phase 4 — Commands, Intents, and history

1. Add branch Commands/Intents.
2. Generalize action commands to target a branch.
3. Add local-condition commands.
4. Add branch reorder and duplication.
5. Protect the last branch.
6. Preserve IDs during reorder.
7. Ensure no-op and failed commands do not enter history.
8. Add Undo/Redo tests for every mutation.
9. Emit explicit Logic Board invalidation/domain changes.
10. Remove obsolete rule-level execution/action commands rather than keeping
    two paths.

## Phase 5 — Compiler and runtime state

1. Compile one trigger per rule.
2. Compile shared conditions once.
3. Compile local predicates for all branches.
4. Precompute all branch eligibility before actions.
5. Reuse the existing execution-mode gate logic.
6. Change state identity to include `branchId`.
7. Emit branches/actions in vector order.
8. Preserve current gate-write and failure behavior.
9. Confirm no Logic API bump is needed.
10. Add generated-Lua golden tests.

## Phase 6 — Native editor UI

1. Keep the single-branch card visually compact.
2. Move the execution selector into the branch header.
3. Add `+ Add Action Group`.
4. Render local conditions inside the branch.
5. Add branch move/duplicate/delete controls.
6. Keep shared conditions in the existing `IF` column.
7. Route every UI action through Intent/Command.
8. Update generated-Lua selection/focus mapping if it references action
   indices.
9. Update collapse/expand behavior.
10. Add lifecycle and visual fixture coverage.

## Phase 7 — Compatibility and cleanup

1. Remove legacy in-memory fields.
2. Remove legacy serializer output.
3. Remove obsolete editor bindings.
4. Search for all direct uses of:
   - `rule.executionMode`;
   - `rule.actions`.
5. Update documentation and examples.
6. Update project fixtures to demonstrate two action groups.
7. Confirm unknown blocks and disabled rules still load.
8. Confirm editor Play and standalone runtime compile the same generated
   program.

## Phase 8 — Verification

Run:

```text
runtime unit tests
logic-board tests
number-expression syntax tests
gameplay characterization tests
editor unit/integration tests
native lifecycle smoke
Fixture Demo Suite
scripts\build.bat --test
```

Then perform an end-to-end authoring check:

```text
WHEN
  On Collision Enter
  Other Type = Coin

THEN GROUP 1 — Every occurrence
  Jump
  Add to Number
  Camera Shake

THEN GROUP 2 — Once per activation
  Play Sound
```

Verify:

- only one trigger is persisted;
- both groups receive the same collision context;
- Group 1 follows current Every-occurrence behavior;
- Group 2 follows current Once-per-activation behavior;
- reorder changes execution order but not gate identity;
- save/reload preserves branch IDs and behavior;
- Generated Lua contains branch-qualified state keys;
- Undo/Redo restores the exact model;
- export/runtime behavior matches editor Play.

---

# Test Matrix

## Migration

```text
legacy rule
→ exactly one branch
→ execution mode preserved
→ actions preserved in order
→ deterministic ID persisted
```

## Independent gates

```text
branch A = EveryOccurrence
branch B = OncePerActivation
→ independent runtime state
→ no cross-branch contamination
```

## Shared and local conditions

```text
shared false
→ no branch runs

shared true
branch A local true
branch B local false
→ only A runs
```

## Snapshot-before-actions

```text
branch A mutates score
branch B local condition reads score
→ branch B sees the pre-dispatch value
```

## Ordering

```text
A before B
→ A actions occur first

reorder B before A
→ B actions occur first
→ IDs unchanged
```

## Context

```text
collision trigger provides Other
→ all branches receive the same Other
→ context is not retained after dispatch
```

## Failures

```text
action throws in branch A
→ existing runtime failure policy preserved
→ no new silent branch isolation
```

## Undo/Redo

```text
add/remove/move/duplicate branch
change execution mode
add/remove/move local condition
add/remove/move action
→ exact state restored
```

## Schema stability

```text
legacy load
→ migrate
→ save
→ reload
→ save again
→ byte-semantic stability
```

---

# Definition of Done

The ADR is implemented only when:

- every rule owns at least one branch;
- rule-level `executionMode` and `actions` no longer exist;
- old boards migrate to one behaviorally equivalent branch;
- new boards serialize only `branches`;
- branch IDs are stable and unique;
- execution state is keyed by branch identity;
- current `Once per activation` behavior is preserved by characterization
  tests;
- shared and local predicates are evaluated before actions;
- branches and actions execute in authored order;
- no second trigger subscription is created;
- all UI mutations use Commands/Intents;
- branch operations support Undo/Redo;
- total action/condition limits are enforced across all branches;
- diagnostics identify the branch;
- the false/redundant execution-mode warning is corrected;
- one-branch rules remain visually compact;
- the motivating two-group rule works in editor Play and standalone runtime;
- runtime and editor test suites are green;
- no project-format, model, or UI compatibility shim remains beyond the
  one-way decoder migration.

---

## Final decision summary

A Logic Board rule will own one authoritative event and an ordered list of
Action Groups. Shared conditions apply to every group; each group may add
local conditions and select its own existing execution mode. All group
eligibility is evaluated before actions, then eligible groups execute in
authored order. Existing execution semantics are preserved exactly, while
duplicated rules and fake sub-events are eliminated.
