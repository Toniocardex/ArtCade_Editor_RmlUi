# ADR-0046 — Restart Scene as a Deferred Lifecycle Request

**Status:** Accepted  
**Date:** 2026-07-29  
**Scope:** `Restart Scene` authoring contract, requester authority, deferred
application, and interaction with Scene Logic.  
**Related:** ADR-0039, ADR-0045, `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§9/§18.

## Context

`Restart Scene` must restore the active scene's authored composition. It is
not an entity mutation and a Scene Logic Board is not a scene runtime owner:
the board is persistent authoring authority, while `GameplaySession` and
`SceneLifecycleService` own runtime activation, teardown and materialization.

The action can originate from an Object Type board, a Scene board, scripts or
future APIs. Giving each origin its own restart path would duplicate lifecycle
authority and risks rebuilding a world during a Logic callback.

## Decision

All origins use one request path:

```text
Logic action Restart Scene
  → RuntimeLogicHostAdapter::requestSceneRestart()
  → PendingSceneRequest(Restart), last request wins
  → GameplaySession safe point
  → SceneLifecycleService::request_restart()
  → transition handler: teardown, materialize, install scopes, lifecycle dispatch
```

The request is recorded only; it is never applied synchronously while a Logic
callback is on the stack. `Restart Scene` is therefore scene-scoped, requires
an active runtime scene, and does **not** require `Self`. It remains available
to both Scene and Object Type boards: the latter request a restart but do not
own it.

The committed restart creates a new scene activation. It cancels the outgoing
Scene Logic scope and Entity Logic scopes, restores the authored layout,
removes dynamic spawns, reinstates authored instances/components/overrides,
installs the incoming Scene then Entity scopes, dispatches `On Scene Start`,
then Entity `On Start`, and only then resumes gameplay ticking. Project
Variables, persistent data and editor workspace state are intentionally not
reset.

`Go To Scene` uses the same pending request channel and lifecycle authority;
the only difference is its destination SceneId. The current last-request-wins
policy remains explicit for multiple requests in a dispatch/frame.

## Guardrails

`On Scene Start → Restart Scene` is structurally valid: a Project Variable
condition can make a one-shot retry useful. It is deferred, so it cannot
re-enter the current callback. The editor should surface a non-blocking
authoring warning when this pair is unconditional; a transition budget for
repeated restarts is a future runtime-safety slice and must be added before a
general automatic-retry feature is promoted.

## Consequences

- No `restartFromEntityLogic` / `restartFromSceneLogic` variants exist.
- Scene Logic gains a valid scene action without acquiring a hidden controller
  entity or a fake `Self`.
- Restart means scene activation, not Restart Game or Reload Project.
- Tests cover the safe deferred restart path and re-dispatch of scene/entity
  activation lifecycle events.
