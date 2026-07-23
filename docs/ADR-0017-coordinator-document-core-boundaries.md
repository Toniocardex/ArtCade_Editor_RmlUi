# ADR-0017 — Coordinator size, ProjectDocument friendship, editor-core/runtime

**Status:** Accepted  
**Date:** 2026-07-23  
**Scope:** Growth policy for `EditorCoordinator`, private mutation surface of
`ProjectDocument`, and the `artcade-editor-core` ↔ runtime link boundary  
**Related:** [Constitution](ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md)
(single authorities; Intent/Command; Play isolation; host-owned process
resources), [Architecture](ARTCADE_RMLUI_ARCHITECTURE.md) §8–9 (document
access; Intent vs Command), Gates (classify before implementing)

## Context

The Coordinator already exposes a large surface of Intents and application
orchestration. That is **not** automatically a P0 violation: there remains a
single coordination authority, and the implementation is already split across
multiple translation units (`editor_coordinator_*.cpp`).

At the same time:

1. Each new feature risks dumping **panel-specific** workflow into the
   Coordinator until it becomes a grab-bag rather than a policy/orchestration
   boundary.
2. `ProjectDocument` uses a long `friend` list so Commands can mutate private
   aggregates. The list is coherent with private mutation, but raises
   maintenance cost. Replacing friendship with public setters would be worse.
3. `artcade-editor-core` links many runtime libraries, including
   `artcade-gameplay-session`, which is appropriate for the unified
   `PlaySession`. That link must not become a license for authoring types to
   depend on simulation internals or process-global host resources.

## Decision

### 1. Coordinator size — role, not “thin at any cost”

Binding split of responsibility:

| Layer | Owns | Must not own |
|---|---|---|
| **`EditorCoordinator`** | Policy, Intent apply / Command execute orchestration, Play gate, workspace reconciliation, invalidation accumulation | Presentation, input buffers, panel layout, long feature-specific scripts that only one panel needs |
| **Focused app services** (`app/*.cpp` workflows) | Multi-step workflows (import, load/migrate, complex inspector commits, asset pipelines) invoked by the Coordinator or thin Intent handlers | A second coordination authority or parallel mutation path around Commands |
| **Panels / UI controllers** | Presentation, Rml markup, local edit buffers that commit via Intent/Command on Enter/blur/explicit action | Direct `ProjectDocument` mutation, runtime ownership, process-global device lifecycle |

**Clause:**

> A large Intent surface is acceptable. Panel-specific logic inside the
> Coordinator is not. Prefer a focused app service over a new Coordinator
> god-method when the workflow is complex and local to one feature family.

Do **not** introduce a second coordinator, event bus, or “panel service registry”
that mutates authoring outside Intent/Command.

Splitting `.cpp` files further for compile/navigation is fine and does not
require splitting the type unless a concrete cohesion problem appears.

### 2. ProjectDocument friendship — private mutation stays

- Keep mutation behind private primitives + Command/`EditorCoordinator`
  friendship (or equivalent limited access).
- **Do not** replace friendship with public setters or mutable collection
  exposure to panels.
- Optional future improvements (only when a **concrete** maintenance problem
  appears — churn, accidental coupling, unreadable friend lists):
  - private patch primitives grouped by aggregate (scene, assets, logic, …);
  - narrower access tokens / friend helpers scoped to a command family;
  - never a general-purpose “edit anything” API for UI.

**Clause:**

> Friendship breadth is a maintenance cost, not a correctness bug. Refactor
> the access surface only when the cost is demonstrated; never by opening the
> document to the UI.

### 3. editor-core ↔ runtime — PlaySession link, authoring firewall

Allowed:

- `artcade-editor-core` / editor app linking runtime modules needed for
  materialize, Play, and host adapters (`artcade-gameplay-session`, logic,
  world, etc.).
- `PlaySession` (and adapters) consuming borrowed host services for
  process-global resources (audio device, window, GL) per Constitution.

Forbidden for the **authoring model**:

- `ProjectDocument`, Commands, and authoring-only types depending on
  simulation frame details, runtime entity handles as persistent authority,
  or process-global Raylib/device APIs.
- Runtime → authoring reverse-sync.
- Initializing or tearing down host process resources inside gameplay
  modules when the editor (or game host) already owns them.

Materialization builds a runtime snapshot/session **from** the document once
at Play start; Edit mode continues to treat `ProjectDocument` as the only
persistent truth.

## Consequences

- New slices: classify work (Gates); put orchestration Intents on the
  Coordinator; put heavy feature workflow in focused app services; keep
  panels presentational.
- No proactive “shrink the Coordinator” or “collapse friends” refactor.
- Reviewers stop the line on: UI→document mutation, panel logic living as
  Coordinator policy, authoring types including simulation headers for
  convenience, or public document setters “to simplify friendship.”

## Verification

- New Intent handlers stay thin (validate → delegate → invalidate) or call a
  named app workflow; no Rml/`Element*` in Commands or core orchestration.
- `ProjectDocument` public API remains const projections + controlled replace;
  friends remain Command/Coordinator-scoped.
- Authoring TUs do not call `InitAudioDevice` / window lifecycle / per-frame
  world mutation APIs; Play path owns that through `PlaySession` + host.
