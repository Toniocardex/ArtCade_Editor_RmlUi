# ADR-0026 — Logic Board Destroy Other (collision target)

**Status:** Accepted  
**Date:** 2026-07-25  
**Scope:** Logic catalog `collision.destroy_other`, Logic codegen, LogicRuntime
context binding, feature gate; no editor UI or PlaySession changes  
**Related:** [`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md) §4
(backlog item "Destroy Other"), ADR-0025 (scene actions), Constitution §21

## Context

Every Logic action targets Self. The single most common gameplay pattern —
*"player touches coin → destroy the coin, add score"* — cannot be authored
directly: today the coin needs its own board that self-destructs on collision
with the player, duplicated per collectible type. The roadmap backlog already
names `Destroy Other`, and every foundation it needs exists:

- collision triggers provide the `EventOther` capability and their generated
  callbacks already receive the collided entity (`function(other)`);
- `blockAvailability()` already gates catalog entries on
  `requiredContext` vs the trigger's `providedContext` (the same mechanism
  that greys out platformer blocks without the controller), and
  `validateBlock` reports `LB_INCOMPATIBLE_BLOCK` through it;
- the runtime host's `requestDestroy` is deferred (queued, flushed after
  event dispatch) and the World destroy handler already cancels the destroyed
  entity's Logic/Script scopes.

## Decision

### Catalog

One new Action descriptor, **no properties**:

- typeId / feature: `collision.destroy_other`
- displayName: **Destroy Other**, category `collision` (beside the collision
  events and `Other Is Object Type`, whose context contract it shares)
- `requiredContext = { EventOther }` — the block is selectable only under
  On Collision Enter/Exit; under any other trigger the picker shows it
  disabled with the availability reason, and validation emits
  `LB_INCOMPATIBLE_BLOCK` (semantic — StructuralCommit stays loadable,
  ADR-0013).

Schema untouched (generic `LogicBlockDef`, no properties). Older runtimes
reject programs via `requiredFeatures`; older editors show an unknown block.

### Codegen

`emitAction` emits `context:destroy_other(other)` — `other` is the collision
callback's parameter, lexically in scope for the whole rule body including
Wait continuations (closures capture it as an upvalue). Placement outside a
collision rule is prevented by validation; the compiler itself never emits a
dangling `other` for valid boards.

### Runtime

`ContextProxy::destroyOther(EntityId other)` → `host.requestDestroy(other)`;
throws (rule disabled + diagnostic, dispatcher intact) on `INVALID_ENTITY`
or host refusal (e.g. the entity was already removed). Destruction is
**deferred** by the existing World queue — never mid-dispatch — and scope
cleanup for the destroyed entity rides the existing destroy handler.
`supportedFeatures` gains `collision.destroy_other`.

### Non-goals

No generalized "target picker" on other actions (Set Visible on Other, …) —
that is a separate semantic (`Entity` targets) with its own authoring UX.
No `Other Has Tag`; no changes to the legacy `Other Is Object Type`
condition.

## Consequences

- The collect/kill loop becomes one rule on the collector's board; per-item
  self-destruct boards are no longer required (still valid where authored).
- Both `Destroy Self` and `Destroy Other` may fire in one rule; both ride
  the same deferred queue.
- If two rules destroy the same Other in one dispatch, the second host call
  still succeeds (the entity is active until the post-dispatch flush) and
  the flush handles the duplicate — no error, no partial state.

## Tests

- Runtime `logic-board-test`: registry (kind/category/feature/no
  properties), availability + `LB_INCOMPATIBLE_BLOCK` under a non-collision
  trigger, compile blocked for invalid placement, codegen emits
  `context:destroy_other(other)` + feature, runtime dispatch destroys the
  collided entity exactly once via the fake host.
- Editor `logic-board-editor-test`: Play integration — Hero board
  On Collision Enter (Other Type = Coin) → Destroy Other (+ Add to Number):
  the coin leaves the renderables, Hero survives; picker availability
  reflects the trigger.
