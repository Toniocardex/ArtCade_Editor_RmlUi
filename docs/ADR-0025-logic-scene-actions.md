# ADR-0025 — Logic Board Scene Actions (Restart Scene / Go To Scene)

**Status:** Accepted  
**Date:** 2026-07-25  
**Scope:** Logic catalog `scene.restart` / `scene.go_to`, `IGameplayRuntimeHost`,
`RuntimeLogicHostAdapter` deferred queue, `GameplaySession::tickFixedStep`,
editor `PlaySession` multi-scene materialization + transition handler,
`SceneReference` property semantic  
**Related:** [`LOGIC_BOARD_RULES_ROADMAP.md`](LOGIC_BOARD_RULES_ROADMAP.md),
ADR-0013 (validator purposes), Constitution §11 (Edit/Play), §21 (Logic Board)

## Context

The Logic Board had no game-flow actions: no authored project could complete
the loop "death → restart" or "door → next level" without hand-written
scripts. The runtime already owns the whole capability —
`SceneLifecycleService.request_load/request_restart`, gateway verbs
(`requestLoadScene`/`requestRestartScene`), scope reinstall on transition in
game.exe's `Application::handleSceneTransition` — but nothing exposed it to
Logic authoring, and the **editor** Play facade passed an empty transition
handler (scene changes from Script `scene.load` silently skipped scope
reinstall in editor Play).

## Decision

### Catalog (logic-core, registry-owned)

- `scene.restart` — **Restart Scene**, Action, category `scene`, no
  properties, feature `scene.restart`.
- `scene.go_to` — **Go To Scene**, Action, category `scene`, one String
  property `sceneId` with the new semantic
  `LogicPropertySemantic::SceneReference`, feature `scene.go_to`.

Schema is untouched (generic `LogicBlockDef`); older builds see unknown
blocks per ADR-0013, older runtimes reject the programs up front via
`requiredFeatures`.

### Validation

`LB_SCENE_REFERENCE` (semantic, never StructuralCommit): `scene.go_to` must
reference an existing scene; empty is an error like Spawn's Object Type. The
editor command path fills a deterministic default (first scene by sorted
`SceneId`, `assignDefaultScene`) so a freshly added action is never red.

### Runtime — deferred, last-wins

Generated Lua calls `context:scene_restart()` / `context:scene_go_to(id)`.
The host adapter **queues** the request (`PendingSceneRequest`, last one
wins — same semantics as the lifecycle service's own pending transition);
`GameplaySession::tickFixedStep` flushes it right before
`tickSceneTransition`, where dispatch depth is zero. Committing
synchronously from a Lua callback would cancel the caller's own scope
mid-rule and drop the incoming scene's On Start (`LogicRuntime::dispatch`
refuses nested dispatch). Actions after a scene action in the same rule
still run against the old scene this frame. An unknown scene at flush time
(compile-gate bypassed) is logged to stderr and skipped — never partial
state.

### Editor PlaySession

- Materializes a `PlaySceneMaterialization` (info, render order, tilemaps)
  for **every** scene at Start Play; a transition swaps the active
  projection — the authoring document is never read mid-Play.
- Wires a real transition handler (parity with game.exe): reinstall
  Logic/Script scopes, then signal the swap through a heap-stable
  `PlaySceneTransitionSignal` (`unique_ptr`, safe across PlaySession moves —
  capturing `this` would dangle on the coordinator's `optional` re-emplace).
  This also fixes the pre-existing editor gap for Script `scene.load`.
- `World::init`'s `replaceProject` bypasses the lifecycle service, so the
  handler never fires during materialize (no double On Start at boot).

## Consequences

- Restart restores authored layout **and** re-fires On Start (asserted:
  position returns to authored + On Start delta, not raw authored).
- Go To fires the incoming scene's On Start exactly once; outgoing entities
  leave the renderables.
- Stop keeps destroying the session; `cancel_transition` already runs in
  `shutdownGraph`.

## Tests

- Editor `logic-board-editor-test` (+46): registry/defaults/RmlUi dropdown
  markup, validation + compile gate, Play integration (go-to, last-wins with
  two requests in one dispatch, On Start refire, restart restore, Edit/Play
  isolation).
- Runtime `logic-board-test` (+30): registry, validation, codegen, runtime
  binding dispatch, empty-id failure isolation.
