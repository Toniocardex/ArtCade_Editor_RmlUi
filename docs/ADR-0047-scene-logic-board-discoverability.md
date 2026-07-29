# ADR-0047 — Explicit Scene Target in the Logic Board Picker

**Status:** Accepted  
**Date:** 2026-07-29  
**Scope:** native Logic Board target-picker discoverability and navigation.  
**Related:** `ADR-0045-scene-logic-scope.md`,
`ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` §4/§5/§8/§12/§17/§21,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§6/§18/§19/§24/§36.

## Context

ADR-0045 made Scene Logic a first-class persisted and runtime scope. The
editor already carries an explicit `Scene(sceneId) | ObjectType(objectTypeId)`
Logic Board target and supports `OpenSceneLogicBoardIntent`.

The visible picker in the Logic Board header, however, lists only Object
Types. The Scene Board can currently be reached only indirectly by opening
Logic Board with no instance selected. That hidden dependency on selection is
not discoverable, led directly to Object Type self-replication being authored
for a scene-wide timer, and makes the screen contradict the ownership model
shown in ADR-0045.

## Decision

Rename the header control conceptually from an Object Type picker to a **Logic
Board target picker**. Its floating menu has two explicit target groups:

1. **Scene** — exactly one entry for the active scene, labelled with its
   display name and a `SCENE` scope marker.
2. **Object Types** — the existing stable, sorted Object Type entries.

The active target receives the existing selected indicator. Selecting the
Scene entry dispatches the existing `OpenSceneLogicBoardIntent` for the active
scene; selecting an Object Type retains `OpenLogicBoardIntent`. Both are
workspace navigation only: they alter `EditorState.logicBoardEditor`, not
`ProjectDocument`.

The header continues to state the selected scope (`SCENE` or `OBJECT TYPE`)
and its existing ownership explanation. The menu is disabled in Play under
the existing authoring policy. No target is inferred from hierarchy selection
when the user explicitly chooses one in the picker.

## Authority, invariants and behaviour

- `ProjectDocument` remains the sole authority for Scene/Object Type Logic
  Board definitions. The picker holds no copy of a board.
- `EditorState.logicBoardEditor` remains the sole workspace authority for the
  selected target. It must contain either a valid `sceneId` or a valid
  `objectTypeId`, never both.
- Navigation is not a persistent authoring mutation: it has no Command,
  Undo/Redo entry, revision, dirty-state or serializer impact.
- The Coordinator validates the active Scene/Object Type ID; an invalid or
  stale target is rejected without changing workspace state.
- Play still rejects authoring changes. Target navigation remains presentation
  and workspace-only, consistent with the existing Logic Board workspace
  policy.
- The floating RmlUi menu remains owned and destroyed by `EditorUi` through
  its existing context-menu lifecycle; no listener, cache or background work
  is added.

## Non-goals

- No Scene Variables, Scene hierarchy node, new Logic blocks, runtime,
  compiler or persistence changes.
- No conversion or migration of an Object Type board into a Scene board.
- No automatic repair of existing self-spawning rules; validation guidance is
  a separate concern.
- No new generic menu, ViewModel, manager, command or target abstraction.

## Alternatives rejected

- **Keep the implicit “no selected entity” entry point:** it is invisible in
  the picker and couples board navigation to unrelated selection state.
- **Add a separate Scene Logic button elsewhere:** duplicates target navigation
  and leaves the header picker misleading.
- **Create a hidden Scene controller Object Type:** violates ADR-0045 by
  giving scene behaviour an artificial entity owner.
- **Persist the last selected target in the project:** workspace navigation is
  not authoring data and must not dirty the project.

## Consequences

- Users can deliberately choose Scene scope before authoring global timers,
  spawning, camera or level-flow logic.
- Existing Object Type navigation remains unchanged and sorted.
- The implementation is limited to the existing Logic Board panel projection,
  Editor UI floating-menu routing and the existing controller intent dispatch.

## Verification

- The target menu renders the active Scene entry and all Object Types, with a
  correct selected marker for each mode.
- Choosing Scene dispatches `OpenSceneLogicBoardIntent`, clears the Object
  Type target and leaves document revision/dirty state unchanged.
- Choosing an Object Type dispatches `OpenLogicBoardIntent`, clears the Scene
  target and leaves document revision/dirty state unchanged.
- The Scene entry remains reachable when an entity is selected and when the
  Scene has no instances.
- The menu is closed before dispatch and cannot author during Play.
- Existing Scene Logic command/persistence/runtime tests and the native Logic
  Board editor test remain green.
