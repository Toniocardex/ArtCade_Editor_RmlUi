# Script Editor roadmap

This roadmap is subordinate to the Architecture Constitution, Architecture,
Engineering Gates and ADR-0002.

| Slice | Status | Evidence / exit gate |
|---|---|---|
| Prerequisite — Runtime host parity | Completed | Shared Logic host is concrete; native/WASM runtime parity tests are green. |
| Script 1 — Asset model and filesystem | Completed | `ScriptAssetDef`, schema v6, Commands, confined atomic file service, create/import, Assets category, round-trip and negative path/UTF-8 tests. |
| Script 2 — Workspace editor MVP | Completed | Third workspace, static textarea surface, tabs, dirty buffers, Save/Save All, local Undo/Redo, unsaved guard, search/go-to-line, UTF-8 cursor status and Script → Play → Script navigation. Native build, 4,369 core assertions and full test suite green; empty/open-file screenshot smoke checks passed. |
| Script 3 — Syntax diagnostics | Completed | 400 ms compile-only Lua 5.4 validation, revision-bound diagnostics, Console/source navigation and strict saved-source validation over an explicit referenced set. Activation becomes non-vacuous when Script 4 authors attachments. |
| Script 4 — Object Type Script Component | Completed | Schema v7 type-owned ordered attachments, Inspector attach/open/enable/reorder/remove, atomic Commands with exact Undo/Redo, delete guard, shared runtime model/parser and strict saved-source Play gate over enabled references. |
| Script 5 — Runtime base | Completed | Shared `script-core`/`script-runtime`, isolated strict Lua scopes, `on_start`/`on_update`, exact saved-source snapshot, bounded execution, error isolation and Editor Play/standalone parity. |
| Script 6 — Gameplay events | Completed | Canonical input and collision callbacks, shared gameplay API, immutable event snapshots, deferred destroy, error isolation and deterministic Logic → Scripts order in Editor Play and standalone. |
| Script 7 — Apply workflow | Planned | Restart-required banner, Save and Restart, workspace return and cursor preservation. |
| Script 8 — IDE polish | Planned | Highlighting and editor assistance only after runtime end-to-end is complete. |

## Script 1 contract

- Authority: metadata in `ProjectDocument`; saved text on disk.
- Intent/Command: catalog mutation uses typed Commands; file service is an I/O
  boundary and cannot mutate the document.
- Invariants: stable unique `AssetId`, unique safe `.lua` path, project-root
  confinement, UTF-8/LF, atomic replacement.
- Undo: metadata only; newly created/imported file is not silently deleted by
  project Undo because the filesystem is not Command history.
- Play: unchanged and no manual script execution.
- Tests: schema migration/round-trip, duplicates, traversal, symlink escape,
  BOM/newline normalization, atomic write/read/fingerprint and Command Undo/Redo.

## Script 2 contract

- Authority: one `ScriptEditorBuffer` per open AssetId in workspace state.
- Intent/Command: open/focus/edit/cursor are workspace intents; metadata stays in
  Commands; Save calls only `ProjectScriptFileService`.
- Invariants: one open tab per asset, active tab resolves, textarea remains a
  static RmlUi element, no panel refresh per keystroke.
- Undo: editor focus routes to buffer history; otherwise project history.
- Play: Start from Script routes to Scene; Stop returns to the same Script state.
- Tests: create → open → edit → save → close/reopen, dirty guard, Save All,
  local Undo/Redo, tab/cursor/scroll preservation and controller contract.

## Script 3 contract

- Authority: diagnostics are a disposable projection of the exact buffer
  revision that was compiled; the buffer remains the only unsaved-source
  authority and the `.lua` file remains the only saved-source authority.
- Intent/Command: validation results enter workspace state through a typed
  `SetScriptDiagnosticsIntent`; validation never dispatches a project Command.
- Invariants: Lua 5.4 source is compiled in text mode without opening libraries
  or calling the produced chunk; a result is accepted only while its source
  revision still matches; Console entries are a replaceable projection keyed by
  Script AssetId, not a second diagnostic store.
- Undo: diagnostics and validation scheduling are derived workspace state and
  never enter project or text Undo history.
- Play: the strict boundary validates only saved scripts referenced by authored
  attachments. Until Script 4 introduces the attachment authority, that set is
  intentionally empty; an unrelated incomplete Script Asset must not block Play.
- Tests: valid/invalid Lua and exact source attribution, compile-without-execute,
  stale-result rejection, save-invalid remains allowed, and strict validation
  over an explicit referenced AssetId set.

## Script 4 contract

- Authority: the ordered `ScriptComponent::attachments` collection lives only
  on each Object Type in `ProjectDocument`; `SceneInstanceDef` has no script
  field or override path. Attachment IDs and Script Asset references are stable.
- Intent/Command: Inspector operations resolve the selected instance to its
  Object Type and dispatch one typed Add/Remove/Move/SetEnabled Command. The
  private document replacement mutator is reachable only by those Commands.
- Invariants: attachment IDs are non-empty and unique within the Object Type;
  every reference resolves to a Script Asset; an empty component is normalized
  to absence; a referenced Script Asset cannot be deleted, even when disabled.
- Undo: each Command captures and restores the exact optional component,
  including order, enabled flags and component presence. Rejected mutations do
  not advance document revision or history.
- Play: the application boundary obtains the deterministic unique set of
  enabled references from `ProjectDocument` and validates the saved `.lua`
  files before either Play mode starts. A missing, unreadable or syntactically
  invalid linked source rejects Play atomically; open buffers are not sampled.
- Tests: Command success/rejection/Undo/Redo, deterministic enabled/all
  reference sets, referenced-delete guard, schema v7 round-trip/migration,
  duplicate/missing/empty validation and native full-suite build gate.

## Script 5 contract

- Authority: `ScriptProgram` in `script-core` is the immutable saved-source
  snapshot for one Play session. `script-runtime` owns disposable VM state;
  neither runtime module reads editor buffers, `ProjectDocument` or files.
- Intent/Command: the application boundary snapshots the exact enabled AssetId
  set before Start Play. Runtime callbacks do not dispatch authoring Commands
  and cannot mutate project history.
- Invariants: one isolated VM per `(entity, attachment)`; API version and return
  shape validated before materialization; structural entity order then persisted
  attachment order; generated Logic dispatch precedes manual callbacks; `on_start`
  is emitted once; late scope installation is rejected.
- Sandbox: no OS, filesystem, debug, dynamic loader, Raylib, network or threads;
  controlled standard-library `require`; finite source, scope, memory,
  instruction and call-depth limits. A callback failure disables only its scope.
- Shared gameplay API: Logic Board and Script runtime use one
  `IGameplayRuntimeHost` authority. Slice 5 exposes `ctx.self:set_visible` and
  `ctx.self:set_position`; the remaining event/gameplay surface belongs to
  Script 6.
- Undo/Stop: runtime mutations are Play-only and have no Undo entry. Stop drops
  all scopes and returns to the untouched authoring document.
- Play/export: Editor Play reads every linked saved file once, then passes those
  exact bytes through validation and materialization. Standalone loading uses
  the same program model, sandbox and lifecycle, with confined bounded reads.
- Tests: invalid API/shape, forbidden libraries, controlled `require`, exact
  snapshot bytes, missing/extra programs, isolated VM state, lifecycle order,
  runtime error isolation, caught limit violations, scope cancellation and
  `ctx.self` mutation in a materialized PlaySession.

## Script 6 contract

- Authority: the existing runtime input source and collision world produce one
  immutable frame/edge projection; `ScriptRuntime` consumes that projection and
  `IGameplayRuntimeHost` remains the sole authority for gameplay mutations.
  Scripts never read `ProjectDocument`, editor buffers, RmlUi or files in a
  callback.
- Intent/Command: gameplay callbacks are Play-only runtime events. They do not
  create authoring Intents or Commands, cannot advance project revision and do
  not enter either project or script-buffer Undo history.
- Callbacks: `on_key_pressed`, `on_key_released`, `on_key_held`,
  `on_collision_enter` and `on_collision_exit` are optional and type-validated.
  Input keys are canonicalized, filtered, sorted and deduplicated before
  dispatch; collision callbacks receive the same `other` both as argument and
  as `ctx.event.other`.
- Shared gameplay API: `ctx.self` exposes visibility, absolute/relative
  position and deferred destruction; `ctx.platformer` exposes move, jump and
  grounded state; `ctx.animation` exposes play/stop/speed; `ctx.audio` exposes
  bounded one-shot playback; `ctx.input` exposes down/pressed/released queries.
  All operations delegate to the same host used by generated Logic Board code.
- Ordering and lifetime: generated Logic consumes the complete immutable input
  or collision snapshot before manual scopes. Manual scopes then run in scene
  structural entity order and persisted attachment order. Destruction commits
  only after every scope in that dispatch phase has run; one callback failure
  disables only its `(entity, attachment)` scope and records source, entity and
  exact callback.
- Undo/Stop: runtime mutations remain disposable and have no Undo entry. Stop,
  scene replacement and entity destruction cancel their owned scopes and clear
  cached collision/input state deterministically.
- Play/export: Editor materialization snapshots the registered animation and
  static-audio catalogs before callbacks can run; runtime callbacks perform no
  document or file lookup. Standalone uses the same `script-core`, Lua host,
  callback contract and `script-runtime` implementation.
- Tests: callback shape and canonical input queries, all shared API families,
  invalid-key and invalid-volume isolation, Logic-before-Script ordering,
  PlaySession input/update, collision `other`, deferred destroy across ordered
  attachments, standalone linkage and native runtime regression coverage.

## Current gate status (2026-07-15)

- `artcade-editor-native.exe`: builds successfully.
- `editor_core_test`: 4,520 passed, 0 failed.
- CTest: editor core, Logic Board and SFX all passed (3/3).
- RmlUi smoke: Script empty state and an opened Lua source render without
  parser warnings; the textarea remains a static document element.
- Slice 6 diff review: no open P0/P1 violations. The shared Lua boundary owns
  key and numeric validation; the volume range is identical in Editor and
  standalone; platformer operations validate the same component contract; no
  direct UI/document access, authoring mutation, duplicate script runtime,
  editor-api bridge, React/Tauri or WASM editor code was introduced.
- Standalone export parity: `runtime-cpp/build/src/app/Debug/game.exe` builds and
  links with the shared Script modules; native `logic_board_test` passes 111/111.
  The only warning is the pre-existing insecure development asset key, which
  must not be shipped.
- Next authorized slice: Script 7 — Apply workflow.
