# Script Editor roadmap

This roadmap is subordinate to the Architecture Constitution, Architecture,
Engineering Gates and ADR-0002.

| Slice | Status | Evidence / exit gate |
|---|---|---|
| Prerequisite — Runtime host parity | Completed | Shared Logic host is concrete; native/WASM runtime parity tests are green. |
| Script 1 — Asset model and filesystem | Completed | `ScriptAssetDef`, schema v6, Commands, confined atomic file service, create/import, Assets category, round-trip and negative path/UTF-8 tests. |
| Script 2 — Workspace editor MVP | Completed | Third workspace, static textarea surface, tabs, dirty buffers, Save/Save All, local Undo/Redo, unsaved guard, search/go-to-line, UTF-8 cursor status and Script → Play → Script navigation. Native build, 4,369 core assertions and full test suite green; empty/open-file screenshot smoke checks passed. |
| Script 3 — Syntax diagnostics | Completed | 400 ms compile-only Lua 5.4 validation, revision-bound diagnostics, Console/source navigation and strict saved-source validation over an explicit referenced set. Activation becomes non-vacuous when Script 4 authors attachments. |
| Script 4 — Object Type Script Component | Planned | Type-owned ordered attachments, Inspector, Commands, reference guards. |
| Script 5 — Runtime base | Planned | Sandboxed runtime, `on_start`/`on_update`, saved snapshot, limits and native/export parity. |
| Script 6 — Gameplay events | Planned | Input/collision/animation/audio, deferred destroy, deterministic Logic → Scripts order. |
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

## Current gate status (2026-07-15)

- `artcade-editor-native.exe`: builds successfully.
- `editor_core_test`: 4,394 passed, 0 failed.
- CTest: editor core, Logic Board and SFX all passed (3/3).
- RmlUi smoke: Script empty state and an opened Lua source render without
  parser warnings; the textarea remains a static document element.
- Next authorized slice: Script 4 — Object Type Script Component. No manual
  script is executed in Play yet; Script 4 supplies the referenced set consumed
  by Script 3's strict saved-source validator.
