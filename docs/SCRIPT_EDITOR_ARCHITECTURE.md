# Script Editor architecture

**Status:** Binding product/architecture specification  
**Date:** 2026-07-19  
**Precedence:** subordinate to Constitution → Architecture → Engineering Gates → ADR-0002  

This document freezes the Script Editor as the **advanced extension of the Logic
Board**, not as an editor for Logic-generated Lua.

## Three product surfaces

| Surface | Role | Editable |
|---|---|---|
| Logic Board | Visual gameplay authoring | Yes |
| Generated Lua | Technical projection of the Logic Board | **No** (read-only, inspect/copy only) |
| Script Editor | Manual Lua for advanced behaviour | Yes |

```
Logic Board ─────┐
                 ├── IGameplayRuntimeHost ── PlaySession
Manual Scripts ──┘
```

Logic and Script must not mutate each other. They share identifiers, gameplay
semantics and the same runtime host. `IGameplayRuntimeHost` is already the
shared boundary for generated Logic programs and manual Scripts.

**Forbidden:** bidirectional Logic Board ⇄ free Lua sync. That creates two
incompatible authorities.

## Authorities (not parallel stores)

| Concern | Authority | Notes |
|---|---|---|
| Script catalog (id, name, path) | `ProjectDocument::scriptAssets` | Commands + project Undo |
| Attachments (order, enabled) | Object Type `ScriptComponent` | Type-owned; no instance override in MVP |
| Saved Lua source | `.lua` file under project root | Never duplicated in project JSON |
| Unsaved draft | `ScriptEditorBuffer` in `EditorState` | Workspace only |
| Executing code | Immutable `ScriptProgram` snapshot | Materialized once at Start Play |

```
file on disk     = last persisted version
editor buffer    = currently edited version
Play snapshot    = exact saved bytes at Start / Restart & Apply
```

Save path (already present): atomic write → verify read →
`MarkScriptBufferSavedIntent`. Play never reads the live buffer, the document
per frame, or files mutated during Play.

## Object Type ownership

```
Object Type: Player
├── Logic Board
└── Script Attachments (ordered)
    ├── PlayerCamera.lua
    ├── AdvancedComboSystem.lua
    └── DamageFlash.lua
```

Each Player instance receives the type Logic Board and every enabled Script
attachment. Runtime creates one isolated VM per `(Entity Instance × Script
Attachment)` and executes attachments in persisted order.

## Deterministic execution order

Within each lifecycle phase:

1. Generated Logic Board  
2. Manual Scripts (attachment order)

Applies to `on_start`, input, update, collision enter/exit.

Frame shape (already explicit in `PlaySession`):

```
Logic input → Script input → Logic update → Script update
→ movement/physics → collision transitions
```

**Documented consequence:** when Logic and Script write the same property in the
same phase, the later Script wins. Do not “fix” conflicts; keep them
predictable.

Editor copy for users:

> Manual Scripts run after Logic Board behaviour within the same lifecycle phase.

## Manual Lua contract (API v1)

Template (creation already emits `require_api_version` + callback table):

```lua
artcade.require_api_version(1)

return {
    on_start = function(ctx) end,
    on_update = function(ctx, dt) end,
    on_key_pressed = function(ctx, key) end,
    on_key_released = function(ctx, key) end,
    on_key_held = function(ctx, key) end,
    on_collision_enter = function(ctx, other) end,
    on_collision_exit = function(ctx, other) end,
}
```

Unknown lifecycle fields that are not functions are rejected by the runtime.

### `ctx` surface (v1)

Present today: `entity_id`, `self`, `platformer`, `animation`, `audio`, `input`,
`event.other`.

**Rotation units:** v1 methods use **radians**. Do not silently switch to
degrees. Prefer an explicit v2 naming (`set_rotation_degrees` /
`set_rotation_radians`) when units are clarified.

**Planned gap — variables:** expose only declared `GameVariableId` values via
`ctx.variables:*`. Scripts must not invent undeclared globals.

## Script API Catalog (single authority for tooling)

Completions, hover, signature help, semantic validation, docs and snippets must
not hardcode parallel string lists in UI / bindings / manuals.

**Home:** `script-core` (`script-api-catalog.h`) beside `kScriptApiVersion`.  
**Executor:** `lua-host` Manual Script strict bindings remain the runtime
authority; the catalog is a tooling projection only.

The catalog feeds:

- runtime binding ↔ catalog parity tests  
- Script Editor autocomplete / hover / signature help  
- semantic validation (IDE-3+)  
- API reference panel  
- snippet generation  

The C++ binding remains the executor; tests assert every public binding has a
descriptor and every descriptor has a binding (`manualScriptRuntimeBindingInventory`).

**IDE-2 delivers** descriptors + parity + editor language service UI.  
**IDE-3/5** add ProjectDocument-backed completions and new host APIs into the
same catalog (never a second list).

## UX integration with Logic Board

### Logic Board advanced menu

- Create Script for Object Type…  
- Attach Existing Script…  
- Open Attached Script  
- View Generated Lua (read-only)

Create Script is an **application transaction** (filesystem + Command + open
workspace), not a hidden file write inside an `EditorCommand`.

### Inspector

`SCRIPTS · OBJECT TYPE` lists attachments in **runtime order** with
Open / Enable / Move / Detach / Reveal. Order shown = order executed.

### Script Editor chrome

Breadcrumb: `Objects / Player / AdvancedMovement.lua`  
Side context: attached Object Type, order index, “After Logic Board”, callback
outline.

## Generated Lua and conversion

Generated Lua stays read-only forever for authoring.

**Rejected:** Convert Logic Board → editable Script with round-trip.  

**Allowed later (one-shot, no link):** Create Script Template from Object Type
(compatible callbacks + comments; never copies generated Lua).

## Logic ↔ Script interoperability

**Phase 1 (current):** same Object Type, `self`, variables, assets, input,
collisions, host, deterministic order.

**Phase 2 (future):** typed Message Definitions with stable IDs — Logic
Send Message / On Message and Script `on_message` / `emit`.  

**Rejected:** Call arbitrary Lua function by string.

## Code editor surface contract

`ScriptEditorState` / `ScriptEditorBuffer` remain text authority.  
`ICodeEditorSurface` is rendering + input only. The surface must not:

- save files  
- know `ProjectDocument`  
- create Script Assets  
- validate the runtime  
- own canonical dirty  
- own application Undo routing  

Target evolution: snapshot/delta apply + diagnostics/completions push, while
edits flow as Intents into the buffer.

**Surface decision (Slice IDE-1):** implement an in-process native Lua editor
surface behind `ICodeEditorSurface`. Do **not** host Scintilla as a Win32 HWND
child over Raylib/RmlUi in the MVP (z-order and single-GL-window conflict).
Scintilla remains a future option only if a stable embedding path is proven.

## Validation layers

1. **Syntax** — debounced Lua parse (~400 ms); no authoring mutation  
2. **Contract** — API version, return table, callback types, size/sandbox limits  
3. **Semantic ArtCade** — asset/variable/clip IDs, API version, component
   requirements; shared with Play/export  

## Save and Play

- Ctrl+S: active Script only (file write; **no** project Undo entry)  
- Save All: every dirty buffer  
- Play uses **saved** sources only  
- Dirty buffers: prefer **Save and Play** / **Cancel** (avoid running invisible
  disk code while the editor shows a draft)  
- During Play: edit allowed; runtime keeps Start snapshot; Save does not patch
  VMs; Restart & Apply validates then replaces the session atomically  
- No partial hot-reload in MVP  

## Sandbox

No filesystem, network, processes, env, `debug`, `io`, `os`, `package`,
`load`/`loadfile`/`dofile`, unconstrained `require`. Document available APIs in
the editor instead of teaching via runtime failures.

## Implementation slices (authorized order)

| ID | Name | Exit focus |
|---|---|---|
| IDE-1 | Commercial editor surface | Replace textarea; highlight; auto-indent; brackets; comment toggle; selection/clipboard; diagnostic decorations; state remains authority |
| IDE-2 | Script API Catalog | Descriptors; signature/hover/snippets; catalog ↔ binding tests |
| IDE-3 | Semantic validation | Assets/variables/requirements → Problems panel |
| IDE-4 | Object Type workflow UX | Create/Attach/Open/reorder/enabled/breadcrumb/context |
| IDE-5 | Variables + missing APIs | `ctx.variables`; completions; explicit transform units |
| IDE-6 | Logic/Script messaging | Typed messages both directions |
| Future | Debugger | Host-controlled; never expose Lua `debug` to scripts |

## Explicit non-goals

- Editable Generated Lua  
- Logic ⇄ Script source sync  
- Lua source inside project JSON  
- Second canonical buffer inside the widget  
- Filename as identity (use `AssetId`)  
- Instance-owned script attachments in MVP  
- Implicit GameVariable creation  
- Direct Raylib / filesystem / network from Scripts  
- Auto hot-reload while typing  
- Applying unsaved buffer text to Play without an explicit action  
- Untyped Call-by-name bridges  
- Parallel hardcoded autocomplete catalogs in UI  

## Definition of Done (product)

The Script Editor is a true advanced extension of the Logic Board when:

- Scripts are persistent assets with stable IDs and on-disk `.lua` sources  
- Drafts live only in workspace buffers  
- Attachments are type-owned and ordered; instances get isolated scopes  
- Logic and Script share `IGameplayRuntimeHost` with Logic-first ordering  
- Generated Lua stays read-only  
- A real code editor provides highlighting and diagnostics  
- Autocomplete/docs derive from one Script API Catalog  
- Asset/variable completions query `ProjectDocument` read-only  
- Play uses immutable saved snapshots; Save-during-Play needs Restart & Apply  
- Scope errors isolate without tearing down the whole runtime  
- No direct Raylib/FS/C++ structure access from Scripts  
- Logic Board remains enough for common gameplay; Scripts add power without
  becoming a second engine  

> The Logic Board is ArtCade’s visual language. The Script Editor is the advanced
> language of the **same** runtime — not the editor of Logic-generated code.
