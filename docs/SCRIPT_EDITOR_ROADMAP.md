# Script Editor roadmap

This roadmap is subordinate to the Architecture Constitution, Architecture,
Engineering Gates, ADR-0002 and [`SCRIPT_EDITOR_ARCHITECTURE.md`](SCRIPT_EDITOR_ARCHITECTURE.md).

## Completed foundation (Script 1–7)

| Slice | Status | Evidence / exit gate |
|---|---|---|
| Prerequisite — Runtime host parity | Completed | Shared Logic host is concrete; native/WASM runtime parity tests are green. |
| Script 1 — Asset model and filesystem | Completed | `ScriptAssetDef`, schema v6, Commands, confined atomic file service, create/import, Assets category, round-trip and negative path/UTF-8 tests. |
| Script 2 — Workspace editor MVP | Completed | Third workspace, static textarea surface, tabs, dirty buffers, Save/Save All, local Undo/Redo, unsaved guard, search/go-to-line, UTF-8 cursor status and Script → Play → Script navigation. |
| Script 3 — Syntax diagnostics | Completed | 400 ms compile-only Lua 5.4 validation, revision-bound diagnostics, Console/source navigation and strict saved-source validation over an explicit referenced set. |
| Script 4 — Object Type Script Component | Completed | Schema v7 type-owned ordered attachments, Inspector attach/open/enable/reorder/remove, atomic Commands with exact Undo/Redo, delete guard, shared runtime model/parser and strict saved-source Play gate over enabled references. |
| Script 5 — Runtime base | Completed | Shared `script-core`/`script-runtime`, isolated strict Lua scopes, `on_start`/`on_update`, exact saved-source snapshot, bounded execution, error isolation and Editor Play/standalone parity. |
| Script 6 — Gameplay events | Completed | Canonical input and collision callbacks, shared gameplay API, immutable event snapshots, deferred destroy, error isolation and deterministic Logic → Scripts order in Editor Play and standalone. |
| Script 7 — Apply workflow | Completed | Restart-required banner, atomic Restart & Apply, pinned Play target, exact workspace return. |

## Authorized IDE slices (from architecture)

These replace the former umbrella “Script 8 — IDE polish”. Product decisions and
non-goals live in `SCRIPT_EDITOR_ARCHITECTURE.md`.

| Slice | Status | Exit focus |
|---|---|---|
| **IDE-1** — Commercial editor surface | **Done** | In-process surface behind `ICodeEditorSurface` (Rml textarea + highlight overlay; no Scintilla HWND). Lua syntax highlight; auto-indent; bracket jump + decoration; comment toggle; indent/outdent; duplicate/move line; caret + current-line overlays; diagnostic gutter + in-text error lines; OS selection/clipboard via textarea; `ScriptEditorState` sole draft authority. |
| IDE-2 — Script API Catalog | **Done** | Central `scriptApiCatalogV1()` in script-core; signature/hover strip; Ctrl+Space completions; API reference panel + snippet insert; catalog ↔ `manualScriptRuntimeBindingInventory` parity tests (145 checks). |
| IDE-3 — Semantic validation | Planned | Asset/variable/clip/requirement checks shared with Play/export; Problems panel. |
| IDE-4 — Object Type workflow UX | Planned | Create Script for Object Type (app transaction); Attach/Open/reorder/enabled; breadcrumb; attachment context panel. |
| IDE-5 — Variables + missing APIs | Planned | `ctx.variables`; completions from `ProjectDocument`; explicit rotation unit APIs for v2. |
| IDE-6 — Logic/Script messaging | Planned | Typed Message Definitions; Logic Send/On Message; Script `on_message` / emit. |
| Future — Debugger | Deferred | Host-controlled breakpoints/step/locals; never expose Lua `debug` to scripts. |

## IDE-1 contract

- Authority: `ScriptEditorBuffer` remains the only unsaved-source authority;
  `.lua` files remain the only saved-source authority; the surface never stores
  a competing draft.
- Intent/Command: text mutations stay workspace Intents; metadata stays Commands;
  Save stays `ProjectScriptFileService` + `MarkScriptBufferSavedIntent`.
- Invariants: one open tab per asset; no panel rebuild per keystroke; diagnostics
  remain revision-bound projections; Generated Lua stays out of this surface.
- Undo: focused editor → buffer history; otherwise project history.
- Play: unchanged — saved snapshots only; Restart & Apply unchanged.
- Tests: text ops (comment/indent/bracket) pure unit tests; controller still
  syncs surface ← buffer; existing Script suite stays green.

## IDE-2 contract

**Classify (Gates §3):** workspace tooling + shared runtime metadata. Not
persistent domain. Not Play. Not a second Script Asset catalog.

### Authority

| Concern | Owner | Notes |
|---|---|---|
| Public Manual Script API v1 surface | C++ bindings in `lua-host` (`pushManualContext` + load capture) | Executor only |
| Tooling descriptors (docs, signatures, snippets) | `Scripts::scriptApiCatalogV1()` in **script-core** | Projection of the binding surface — never `ProjectDocument` |
| Binding path inventory | Same registration tables as `pushManualContext` / callback capture | Exposed for parity tests |
| Unsaved Lua text | `ScriptEditorBuffer` | Unchanged |
| Asset/variable/clip IDs | `ProjectDocument` (read-only later) | **Out of IDE-2** → IDE-3/5 |

### Intent / Command

- **None** for catalog data (static process metadata).
- Completions/snippets that edit text use existing `EditScriptBufferIntent` only.
- No Command, no project dirty, no Undo entry for opening the API panel.

### Invariants

- One catalog feeds hover, signature help, snippets, API reference panel, and
  parity tests. No parallel hardcoded string lists in UI.
- Catalog `apiVersion` matches `kScriptApiVersion`.
- Every public binding path has a descriptor; every descriptor path exists in
  the runtime inventory (bijection).
- Catalog must not invent APIs absent from Manual Script strict sandbox
  (no Sol2 `game-api`, no `debug`, no filesystem).
- `ICodeEditorSurface` remains rendering/input; language hints are controller
  projections, not buffer authority.
- Generated Lua stays out of this surface.

### Undo / Play

- Undo: only if a completion/snippet mutates the buffer (buffer history).
- Play: unchanged — catalog is not consulted at runtime.

### IDE-2 MVP scope

| In | Out (later) |
|---|---|
| Descriptors for globals, callbacks, `ctx` fields, methods | `ctx.variables`, degree rotation APIs (IDE-5) |
| Parity tests catalog ↔ binding inventory | Semantic asset/variable validation (IDE-3) |
| Hover / signature strip from token under caret | ProjectDocument-driven completions (IDE-3/5) |
| Ctrl+Space completions + insert snippets from catalog | Full debugger (Future) |
| Read-only API reference strip in Script workspace | Logic Board messaging (IDE-6) |

### Tests

- Pure unit: catalog non-empty, unique qualified names, version == 1,
  required callbacks present, snippet insert texts non-empty for methods.
- Parity: sorted catalog binding paths == `manualScriptRuntimeBindingInventory()`.
- Language service: resolve hover/signature/completions for known tokens;
  unknown token → empty.
- Existing Script / editor suites stay green.

## Historical contracts (Script 1–7)

The Script 1–7 contracts below remain the acceptance record for the foundation
slices. Do not reopen them unless a P0 authority violation appears.

<details>
<summary>Script 1–7 contract text (unchanged)</summary>

### Script 1 contract

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

### Script 2 contract

- Authority: one `ScriptEditorBuffer` per open AssetId in workspace state.
- Intent/Command: open/focus/edit/cursor are workspace intents; metadata stays in
  Commands; Save calls only `ProjectScriptFileService`.
- Invariants: one open tab per asset, active tab resolves, textarea remains a
  static RmlUi element, no panel refresh per keystroke.
- Undo: editor focus routes to buffer history; otherwise project history.
- Play: Start from Script routes to Scene; Stop returns to the same Script state.
- Tests: create → open → edit → save → close/reopen, dirty guard, Save All,
  local Undo/Redo, tab/cursor/scroll preservation and controller contract.

### Script 3 contract

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

### Script 4 contract

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
  reference sets, referenced-delete guard, schema round-trip/migration,
  duplicate/missing/empty validation and native full-suite build gate.

### Script 5–7

See git history / prior roadmap revisions for the full Script 5–7 contract
paragraphs (runtime base, gameplay events, Restart & Apply). Behaviour remains
in force; IDE slices must not violate those authorities.

</details>

## Current gate status (2026-07-19)

- Foundation Script 1–7: complete.
- Product architecture written: `SCRIPT_EDITOR_ARCHITECTURE.md`.
- **IDE-1** commercial editor surface: complete (in-process overlay behind
  `ICodeEditorSurface`; no Scintilla HWND).
- **IDE-2** Script API Catalog: complete (script-core descriptors, editor
  language service, binding parity).
- Next authorized work: **IDE-3** Semantic validation.
