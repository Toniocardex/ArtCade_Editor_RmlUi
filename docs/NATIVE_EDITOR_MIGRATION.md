# Native Editor Migration Matrix

Questo documento traccia la migrazione dal vecchio editor React al nuovo editor
nativo RmlUi. La migrazione procede per feature/caso d'uso, non per pannello.

Regola: se il vecchio percorso React confligge con
`RMLUI_MIGRATION_CONTRACT.md`, vince il contratto.

## Paletti architetturali (perche' esiste questa migrazione)

L'editor React e' diventato troppo complesso non per quantita' di feature, ma
per *forma*: piu' fonti di verita' per lo stesso dato, piu' entry point per la
stessa operazione, orchestratori e sync che tenevano allineate copie che non
sarebbero dovute esistere.

Il nuovo editor esiste per eliminare quella forma, non per riprodurla in C++.
Ogni operazione — creare un'entita', rinominarla, spostarla, creare o eliminare
una scena, cambiare un asset, gestire un componente — deve rispettare questi
paletti, sempre, non solo nello spike:

1. **Una sola fonte di verita'.** `ProjectDocument` e' l'unica autorita'
   persistente per scene, entita', componenti, asset, Logic Board e variabili.
   Nessun `UiProjectModel`, `InspectorModel` o `RuntimeCopy` autorevole in
   parallelo. I pannelli leggono via query e modificano solo via comando.
2. **Un solo entry point per operazione.** Ogni operazione passa per esattamente
   un percorso: `execute(EditorCommand)` se entra in salvataggio/undo,
   `apply(EditorIntent)` se tocca solo il workspace. Mai una seconda strada
   "diretta" per la stessa modifica.
3. **Un solo coordinatore.** `EditorCoordinator` e' l'unico punto di
   coordinamento. Niente catene pannello -> pannello, callback circolari,
   event bus string-based, service locator.
4. **Nessuna sincronizzazione.** Niente sync service, polling dello stato
   authoring, fingerprint, readiness flag tra oggetti dello stesso processo,
   retry per modifiche locali, refresh globale per frame, serializzazione
   interna tra moduli. L'invalidazione e' esplicita, tipizzata e consumata una
   volta per frame.
5. **Il flusso deve restare spiegabile in una riga.** Il test di riferimento di
   ogni feature e': `evento UI -> command/intent -> ProjectDocument/EditorState
   -> invalidazione mirata -> frame successivo`. Se serve un diagramma con piu'
   di un ramo per spiegare una singola modifica, la feature va semplificata,
   non portata com'e'.

Regola pratica, prima di aggiungere qualunque classe: *"elimina complessita'
reale o nasconde un nuovo percorso di sincronizzazione?"*. In caso di dubbio:
la soluzione piu' diretta, tipizzata e locale.

Dettaglio normativo completo in `RMLUI_MIGRATION_CONTRACT.md` (§Autorita',
§Direzione delle dipendenze, §Divieti) e nel prompt di refactor
`ARTCADE_RMLUI_CLAUDE_REFACTOR_PROMPT.md` (§2, §3, §25). Questi paletti non sono
criteri del solo spike: valgono per ogni feature di ogni fase qui sotto, finche'
il vecchio editor non e' rimosso.

## Cadenza di lavoro

I paletti sopra sono rigidi. La cadenza di lavoro non lo e': si interviene solo
quando si protegge un rischio reale, non per purezza fine a se stessa.

Bloccare sempre, senza compromessi, quando un incremento introduce:

- doppie fonti di verita';
- mutazioni fuori da Command/Intent (o, in Play, fuori dal confine runtime
  esplicito del coordinator);
- polling o sincronizzazione nascosta;
- runtime che legge l'authoring durante Play;
- renderer che legge direttamente il dominio;
- dipendenze invertite;
- perdita dati;
- invalidazioni o persistenza incoerenti.

Non fermare il lavoro per:

- accessor migliorabili ma gia' corretti;
- nomi non perfetti;
- wrapper aggiuntivi;
- astrazioni preventive;
- edge case teorici senza impatto reale;
- refactor che non sbloccano una capability concreta.

Criterio operativo per ogni incremento:

```text
capability visibile
-> implementazione end-to-end
-> test/build/smoke
-> chiusura
-> step successivo
```

Non:

```text
feature -> audit -> micro-refactor -> secondo audit -> wrapper
       -> terzo audit -> feature mai conclusa
```

La baseline e' considerata solida. Ogni incremento deve produrre valore
funzionale reale; la pulizia architetturale e' ammessa solo quando protegge un
paletto o sblocca la capability in corso.

## Fasi

1. Fondazioni: `ProjectDocument`, `EditorState`, `SelectionState`,
   `EditorCoordinator`, command, intent, `DomainChange`, invalidation.
2. Vertical slice: open project, scene selection, entity selection, transform
   editing, viewport, undo, save, reload.
3. Struttura editor: scene create/delete, entity create/delete, components,
   asset references, validation.
4. Asset pipeline: stable asset IDs, import, metadata, reimport, missing asset
   handling.
5. Logic Board: board document, commands, compiler, diagnostics, generated Lua.
6. Play Session: start, pause, stop, debug query, runtime isolation.
7. UI polish: drag-and-drop, context menu, shortcuts, dialogs, empty states,
   accessibility, DPI.
8. Rimozione vecchio editor: solo quando non esistono piu' doppi percorsi.

## Matrix

| Feature | Vecchia autorita' | Nuova autorita' | Stato | Vecchio percorso rimosso |
| --- | --- | --- | --- | --- |
| Native RmlUi shell | React DOM / CSS | RmlUi document + `RmlHost` | Done | No |
| Font rendering | Custom `rlgl` Rml renderer | RmlUi `RenderInterface_GL3` | Done | Yes for native target |
| Scene selection | React/editor active state | `EditorState.activeSceneId` | In progress | No |
| Entity selection | React/editor selection state | `SelectionState` via `SelectEntityIntent` | In progress | No |
| Transform edit | TS project/store path | `SetEntityPositionCommand` -> `ProjectDocument` | In progress | No |
| Entity rename | TS project/store path | `RenameEntityCommand` -> `ProjectDocument` | In progress | No |
| Scene background edit | TS project/store path | `SetSceneBackgroundCommand` -> `ProjectDocument` | In progress | No |
| Scene properties (Scene Inspector) | TS scene settings panel | No-selection Inspector shows the active scene: GENERAL (`RenameSceneCommand`, ID, Set as Start, entity count), WORLD BOUNDS (`SetSceneSizeCommand` in `wu`; resize never moves instances; Fit View to Bounds = workspace camera), DIAGNOSTICS (entities outside bounds, derived) | Done | No |
| Scene layers | React layer panel | Per-scene `SceneDef.layers` (order authority) + `defaultLayerId`; `EntityInstance.layerId` membership; `scene_layer_commands` (Add/Rename/Move/Remove/SetEntityLayer); active/hidden in `EditorSceneViewState` (intents); layer-ordered snapshot collector; Scene Inspector LAYERS + Entity layer picker | Done | No |
| Scene create | TS project/store path | `CreateSceneCommand` -> `ProjectDocument` | Done | No |
| Scene delete | TS project/store path | `DeleteSceneCommand` -> `ProjectDocument` (exact undo) | Done | No |
| Entity create (+Entity) | TS project/store path | atomic `CreateEntityWithDefaultTypeCommand` -> always a NEW `ObjectTypeDef` + its instance (independent object); never reuses a type | Done | No |
| Add Instance of a type | TS project/store path | `addInstanceOfSelectedType` -> `CreateEntityCommand(existingTypeId)` (reused) + `SelectEntityIntent`; new instance shares the type's components (prefab semantics) | Done | No |
| Entity delete | TS project/store path | `DeleteEntityCommand` -> `ProjectDocument` (index-faithful undo) | Done | No |
| Hierarchy add/delete wiring | React Hierarchy buttons | `hierarchy_actions` (UI-free) -> `EditorCoordinator` | Done | No |
| Start scene | TS project/store path | `SetStartSceneCommand`; first scene auto-keeps invariant | Done | No |
| Workspace reconciliation | React effects/listeners | `EditorCoordinator::reconcileWorkspace` (same op) | Done | No |
| Undo / Redo | React/editor history path | `CommandStack` (undo+redo) + toolbar buttons & Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z (single `undo`/`redo` coordinator entries); enabled derived from `can(Un\|Re)do() && !isPlaying()`; per-entry revision restore keeps dirty correct across the walk | Done | No |
| Project replace/load boundary | React/Tauri file path | `EditorCoordinator::replaceProject(ProjectDocument)` | In progress | No |
| Play Project | WASM bridge / preview path | `EditorCoordinator::playProject` (guarded by `canPlayProject`) | Done | No |
| Play Current Scene | WASM bridge / preview path | `EditorCoordinator::playCurrentScene` (guarded by `canPlayCurrentScene`) | Done | No |
| Project file I/O | React/Tauri file path | `readProjectTextFile` + `loadProjectFromText` + atomic save, wired to GUI Open/Save/Save As (native pickers; app clears texture cache on replace) | Done | No |
| Runtime viewport | WASM/runtime preview | `SceneFrameSnapshot` + derived texture cache | In progress | No |
| Viewport pick + drag | React canvas pointer handlers | `pickEntityAt` + `SelectEntityIntent`; drag preview local, one `SetEntityPositionCommand` on release | Done | No |
| Scene view navigation | React canvas scroll/zoom | Per-scene `EditorSceneViewState{pan,zoom}` (workspace); MMB / Space+LMB pan; wheel zoom under cursor; Fit View; first-open auto-fit; toolbar zoom %/reset; one shared `makeSceneViewCamera` | Done | No |
| Authored runtime motion | Logic Board / Lua runtime | `EntityDef.linearMover` -> `RuntimeEntity.velocity` -> `PlaySession::advance` via `advanceRuntime`; edited via `linear_mover_commands` + Inspector, persisted in the object-type subset | Done | No |
| TopDownController (input) | Logic Board / Lua runtime | `EntityDef.topDownController` -> `RuntimeTopDownController` -> `PlaySession::update` via `updateRuntime` with `RuntimeInputSnapshot`; edited via `top_down_controller_commands` + Inspector, persisted | Done | No |
| Runtime AABB collisions | Logic Board / Lua physics | `RuntimeBoxCollider` materialized from `EntityDef.boxCollider2D`; both movers route through `PlaySession::moveKinematicEntity` (per-axis swept clamp vs static solids); mover-vs-mover and triggers out of scope | Done | No |
| PlatformerController | Logic Board / Lua runtime | `EntityDef.platformerController` (Move Speed/Jump Speed/Gravity -> canonical maxSpeed/jumpForce/customGravity) -> `RuntimePlatformerController`; gravity + edge jump via `KinematicMoveResult`; `platformer_controller_commands` + Inspector, persisted; single movement driver enforced | Done | No |
| Unsaved-changes guard | React beforeunload / dialogs | `resolveUnsavedGuard` (pure) + native confirm; guards New, Open and Exit (Save/Discard/Cancel, Save-fail aborts); New/Open blocked during Play | Done | No |
| New project | React/Tauri new-project path | App `newProject` (File > New): guard -> `replaceProject(ProjectDocument{ProjectDoc{}})` -> clear path -> "Untitled" title; empty/clean/history-less | Done | No |
| Play materialization | WASM bridge / preview path | `PlaySession` from `ProjectDocument` once at Start Play | In progress | No |
| Sprite Renderer component | React Inspector | `sprite_commands` + `inspector_actions` (instance-scoped) | Done | No |
| BoxCollider2D component | React Inspector / physics form | `box_collider_commands` on `EntityDef.boxCollider2D` | Done | No |
| Object type persistence | React project store | `ProjectSerializer` minimal subset + referential validation | Done | No |
| Components inspector | React Inspector | Feature commands + read-only queries | In progress | No |
| Asset references | React asset stores | `AssetId` -> `ProjectDoc.imageAssets.sourcePath`, validated | In progress | No |
| Asset import (image/audio/font) | React asset import/store | One `importAsset(AssetKind,...)` pipeline: picker -> copy into `<projectRoot>/assets/{images,audio,fonts}` -> typed `Add{Image,Audio,Font}AssetCommand` (relative path); Assets panel lists/imports/removes per kind; images also assignable | Done (import); audio/font consumers pending | No |
| Console copy | React console copy/inspect | Local panel row selection + raylib `SetClipboardText` via one `copySelectedConsoleMessage`; Copy button + Ctrl+C; pure `formatConsoleMessageForClipboard` | Done | No |
| Logic Board | React Logic Board state | Logic Board document + commands | Planned | No |

## Object type creation invariant

Every entity the editor creates must immediately reference a **real, persisted**
`ObjectTypeDef` — never a sentinel id. The earlier `defaultObjectTypeId` returned
the string `"Entity"` as a fallback when the catalog was empty, so the first
entity in a fresh project pointed at an object type that did not exist; every
object-type-scoped component command (`AddTopDownController`, `AddBoxCollider`,
`AddLinearMover`) then failed with "Unknown object type: Entity".

The fix lives at the single canonical creation path, not scattered across the
component commands. **"+Entity" always creates an independent object** — a new
object type plus its first instance — never a reuse of an existing type:

```text
Add Entity
-> objectTypeId = makeUniqueObjectTypeId(document)   ("object-N", always new)
-> CreateEntityWithDefaultTypeCommand
   └─ creates a real ObjectTypeDef (id "object-N", name "Entity")
   └─ creates the instance referencing that id
-> one command, one undo/redo entry
```

Reusing a type — placing another instance that *intentionally* shares its
components — is the separate **Add Instance** operation (`addInstanceOfSelectedType`,
see below). The two stay distinct:

```text
Add Entity              -> new ObjectTypeId + new EntityId   (independent object)
Add Instance of a type  -> existing ObjectTypeId + new EntityId  (shared components)
```

Why a fresh `EntityId` is not enough: `BoxCollider2D`, `LinearMover` and
`TopDownController` are **object-type-owned**, so two instances of the same type
share them by design (correct for prefabs/enemies). For two *independent* objects
each must own a distinct `ObjectTypeId`; otherwise adding a collider to one shows
up on the other.

`CreateEntityWithDefaultTypeCommand` is atomic: it validates both ids up front
(no partial mutation possible), creates the type then the instance, and its undo
removes exactly both. Redo re-applies the stored ids — it never generates new
ones. The id is an internal token (`object-N`); the display name (`"Entity"`) is
separate and is what the Inspector's Type field shows (it reads
`findObjectType(id)->name`, falling back to the id for a legacy/catalog-less
instance). No `NewProjectService`, no two coordinated UI commands, no hidden
fallback inside the component commands, no transaction manager.

Invariant: after any authoring command, every `SceneInstanceDef.objectTypeId`
resolves to an existing `ObjectTypeDef`. The only tolerated exception is loading a
legacy catalog-less file (kept for compatibility); the editor itself never
generates new inconsistent state. Covered by editor-core tests: empty-catalog Add
Entity creates type + instance, components work at once, undo/redo restores both
with the same ids, a non-empty catalog reuses its type, a colliding create makes
no partial mutation, and the type + reference survive save/reload.

## Component resolution (sprite renderer)

A sprite renderer can exist on two levels: the object type (`EntityDef.sprite`)
and a per-instance override (`SceneInstanceDef.spriteRenderer`). A single query,
`resolveSpriteRenderer(document, sceneId, entityId)`, is the only resolver used
by both the viewport and the Inspector. Precedence:

```text
instance override present        -> use the override        (InstanceOverride)
else object type sprite w/ image -> use the inherited sprite (EntityDefinition)
else                             -> no sprite renderer       (None)
```

Consequence to keep in mind: because the override is a `std::optional`, **Remove
Override means "drop the override and fall back to the inherited component"**, not
"disable the inherited component for this instance". If a true per-instance
disable is ever needed, `optional` is not enough — it would take an explicit
`Inherit | Override | Disabled` mode — but that is deliberately not introduced
without a concrete use case.

Object types now persist (minimal subset: `id`, `name`, `visible`, `sprite`
asset + fill — not the full `EntityDef` bag). So an inherited sprite survives
save/reload, an override still prevails after reload, and removing the override
falls back to the base after reload. The validator rejects a duplicate object
type id (on deserialize) and, when a catalog exists, an instance whose
`objectTypeId` is dangling. The serializer never copies the inherited component
into each instance. The format addition is backward-compatible (a file without
`objectTypes` loads with an empty catalog), so no schema bump is required yet.

Mutation detection is revision-based, not flag-based: `executeOwned` compares
`ProjectDocument::revision()` before and after `apply()`. A command changed the
project iff the revision moved; debug asserts pin the contract (a failed command
must not mutate; a no-op must declare no change and no invalidation; a mutating
command must declare both).

## Component ownership matrix

The native editor now covers three ownership shapes, intentionally:

```text
Transform       -> instance only
Sprite Renderer -> object type inheritance + optional instance override
BoxCollider2D   -> object type only, shared by every instance of that type
```

`BoxCollider2D` lives on `EntityDef.boxCollider2D` and is edited through
commands that target the object type id directly. The selected instance is used
only by the Inspector to discover the authoritative object type. There is no
per-instance override, no `resolve*` layer, and the serializer never writes the
collider into `SceneInstanceDef`. The viewport consumes projected
`SceneFrameCollider` values (`entityId`, world bounds, enabled/trigger/selected
flags) from `collectBoxColliderBounds(...)`; draw code does not re-read
`EntityDef` to interpret collider ownership.

## Edit viewport texture baseline

The Edit viewport renders from an immutable projection:

```text
ProjectDocument + EditorState
-> SceneFrameSnapshot
-> SceneView
```

`SceneFrameSnapshot` contains entity placeholders, `SceneFrameSprite` draw
items, and `SceneFrameCollider` overlays. It carries `AssetId`, destination
bounds, visibility, and selection state, but no `Texture2D` and no GPU handle.

Texture resources are derived and non-authoritative. Persisted `sourcePath`
values should be portable paths relative to the project/resource root, not
machine-specific absolute paths:

```text
SceneFrameSprite.assetId
-> ImageAssetDef.sourcePath
-> application resolves project/resource root + sourcePath
-> TextureCache
-> DrawTexturePro
```

`TextureCache` belongs to the native rendering layer. It receives resolved paths,
loads synchronously, records failed loads to avoid retrying every frame, unloads
while the Raylib context is still valid, and is not serialized. Missing source
paths or missing files produce a diagnostic placeholder; they do not mutate the
document.

The application path that consumes `DomainChange::ProjectReplaced` must call
`TextureCache::clear()` directly, so two projects can reuse the same `AssetId`
without stale GPU state. This must not be discovered by polling
`ProjectDocument::replaceCount()` in the frame loop. `TextureCache::invalidate(id)`
exists for future catalog changes where `sourcePath` changes but the `AssetId`
does not.

The renderer must not query `ProjectDocument`, `EditorCoordinator`, RmlUi
controls, or panels during draw. Asset catalog lookup happens before drawing;
`SceneView` receives only `SceneFrameSnapshot` and `TextureCache`.

## Play materialization baseline

Play now has a concrete runtime boundary:

```text
Play Project
-> ProjectDocument.startSceneId
-> materialize PlaySession

Play Current Scene
-> EditorState.activeSceneId
-> materialize PlaySession
```

Materialization reads `ProjectDocument` once at Start Play, resolves each
instance's Sprite Renderer through the same authoring resolver used by Edit,
and creates only the runtime subset needed by this slice:

```text
RuntimeScene
-> RuntimeEntity
-> optional RuntimeSpriteComponent

PlayAssetCatalogSnapshot
-> only image assets referenced by the materialized scene
```

After Start Play:

```text
PlaySession
-> SceneFrameSnapshot
-> SceneView
```

The Play draw path does not query `ProjectDocument`, object types, Inspector
state, RmlUi controls, or JSON. The application resolves the frozen
`PlayAssetCatalogSnapshot` source paths into `TextureRequest`s and feeds the
same derived `TextureCache`. `Stop` destroys the session and returns to the Edit
projection; it never writes runtime state back into the authoring document.

Current policy: `replaceProject()` while Play is active is rejected explicitly.
This avoids a hidden auto-stop or frame-loop observer.

Authoring edits are also blocked while Play is active:

```text
isPlaying()
-> EditorCommand rejected
-> undo rejected
-> ProjectDocument revision/dirty/history unchanged
```

Workspace intents may still run when they do not mutate the document. This is a
coordinator-level rule, so it applies equally to RmlUi buttons, menu actions,
shortcuts and tests.

The UI mirrors this freeze as an affordance across **every authoring surface**
(enforcement stays in the coordinator). The rule is blunt and unambiguous: Edit =
modify the project; Play = observe a frozen runtime copy; Stop = back to authoring.
While Play runs, read-only is applied to:

```text
Inspector fields + component Add/Remove + asset assignment
Hierarchy create/delete (Scene, Entity, Set Start)
Assets import/remove/use
Viewport drag
Undo / Redo
```

and these stay usable (workspace-only, no authoring mutation):

```text
entity selection, scene navigation (tabs), pan/zoom, console, Stop
```

This fixes a real UX trap: an enabled field let the user type e.g.
`Speed = 1000` during Play; the commit was silently rejected, so Stop + Play
never saw the value — it looked like ArtCade ignored the edit. An
`<input disabled>` in RmlUi cannot take focus or typing
(`ElementFormControl::IsDisabled()` is attribute-presence based), so no
misleading uncommitted buffer can form; `<button>` is a plain element, so its
`disabled` class is visual only and clicks still reach the coordinator (rejected,
consistent with the disabled toolbar buttons). For the state to toggle,
`playProject` / `playCurrentScene` / `stopPlaying` invalidate
`Toolbar | Viewport | Inspector | Hierarchy | Assets`, so every panel re-renders
frozen on Start and editable on Stop — the re-render also rebuilds the Inspector
inputs from authoritative values, discarding any uncommitted buffer (no ghost
`1000` left in a field whose real value is `100`).

Live editing during Play (hot-reloading authoring into the running session) is
deliberately **not** implemented: it would need a sync policy between two worlds
(which components update, when, and how undo interacts). If wanted later it must
be an explicit, separate "Live Edit" mode — not an ambiguous exception to this
rule.

During Play, scene-selection intents affect only the workspace:

```text
SelectSceneIntent(B)
-> EditorState.activeSceneId = B
-> current PlaySession remains on its materialized source scene
-> Stop returns the viewport to the Edit projection for B
```

Blocked commands may append a console warning and invalidate Console. They must
not change `ProjectDocument`, revision, dirty state or undo history.

The toolbar should label the runtime target, for example `PLAYING - Scene A`.
That label is derived from `PlaySession::scene()` and exists only to avoid UX
ambiguity when the workspace active scene changes during Play.

Runtime mutations flow through narrow coordinator entry points, never a mutable
session handle:

```text
EditorCoordinator::advanceRuntime(dt)          // autonomous motion (LinearMover)
-> PlaySession::advance
EditorCoordinator::updateRuntime(input, dt)    // input-driven (TopDownController)
-> PlaySession::update(RuntimeInputSnapshot, dt)
-> RuntimeEntity.transform.position
-> Play SceneFrameSnapshot
```

Neither is an `EditorCommand`; neither touches `ProjectDocument`, undo, revision,
dirty state or JSON. The coordinator exposes the session read-only
(`const PlaySession*`) and keeps the mutable surface private, so panels, toolbar
and shortcuts cannot open parallel mutation paths. `Stop` destroys the session,
so the next Play starts again from the authoring document.

The first *authored* runtime behaviour is linear motion, driven by data rather
than a hardcoded loop rule (the earlier WASD smoke harness is removed):

```text
EntityDef.linearMover (canonical component, object type)
-> materialize: RuntimeEntity.velocity = normalize(direction) * max(0, speed)
-> advanceRuntime(dt): position += velocity * dt   (each Play frame)
-> Play SceneFrameSnapshot
```

The runtime integrates whatever the authoring document declares; `editor_app`
holds no per-entity movement rule. The mover is authored end-to-end: edited from
the Inspector via `linear_mover_commands` (object-type scope, same pattern as
BoxCollider2D, undoable) and persisted in the object-type subset by
`ProjectSerializer`. `_paused` stays a runtime flag and is deliberately not
serialized. Mover edits invalidate only the Inspector — motion has no edit-mode
viewport visual; it is observed in Play, which renders every frame.

The first *input-driven* behaviour is the canonical `TopDownController`. It closes
the full gameplay loop: authored in the Inspector (this slice edits the speed =
`maxSpeed`; acceleration/friction/fourDirections persist untouched), materialized
into `RuntimeTopDownController`, and moved each Play frame by a
`RuntimeInputSnapshot` the application builds from the platform:

```text
Raylib keys -> RuntimeInputSnapshot -> EditorCoordinator::updateRuntime(input, dt)
-> PlaySession::update: direction = normalizeOrZero(right-left, down-up)
-> position += direction * speed * dt   (each controller entity)
```

`PlaySession` never sees Raylib. Opposite inputs cancel, the diagonal is
normalized (never faster — a fixed behaviour, not a property), non-finite or
non-positive `dt` is a no-op, and input is neutral while an RmlUi text field has
focus. Edits use `top_down_controller_commands` (object-type scope, undo/redo,
Inspector-only invalidation) and persist in the object-type subset.

## Runtime collisions baseline

The first runtime physics slice gives the gameplay loop solid ground: a moving
entity is blocked by solid colliders instead of passing through. It is a
kinematic-vs-static model, deliberately not a physics engine.

```text
kinematic mover  = entity moved by TopDownController or LinearMover
static solid     = entity with an enabled, non-trigger BoxCollider2D and no mover

mover vs static solid -> resolved
mover vs mover        -> not resolved (out of scope)
trigger / disabled    -> never blocks
```

Both movers share **one** internal entry point —
`PlaySession::moveKinematicEntity(entity, desiredDelta)` — so `advance`
(LinearMover velocity) and `update` (TopDownController input) cannot drift into
two collision systems. There is no `PhysicsManager`, registry or event bus.

Resolution is per-axis swept, against the static solids frozen at materialize:

```text
resolve X (clamp desiredDelta.x to the nearest solid the mover overlaps on Y)
-> apply X
resolve Y (re-evaluate overlap with the new X)
-> apply Y
```

Resolving the axes independently produces natural sliding (a side wall stops X
while Y continues; a floor stops Y while X continues) — slide is the canonical
behaviour, not a configurable mode. The per-axis **clamp** (allowed gap to the
nearest solid, never more than the requested move) is a 1-D sweep, so a fast
mover stops exactly at contact and never tunnels through a thin wall even at large
`dt`. Touching edges are not overlapping (strict inequality), so a mover sits
flush against a wall and still slides; an already-penetrated pair is never
auto-depenetrated (the mover can move out, no teleport).

The authoritative collider AABB is one function,
`runtimeColliderBounds(entity)` = `position + offset ± size/2`, mirroring the
editor's collider draw convention (no sprite/texture/`ProjectDocument` lookup).
The scene rectangle is **not** a collider: it clips rendering only; to confine an
entity you place colliders at the edges. Triggers are materialized but inert
(no block, no enter/exit events) — their consumer is a later slice.

This stays pure runtime: `moveKinematicEntity` mutates only `RuntimeEntity`
transforms; `ProjectDocument`, revision, dirty and history are untouched, `Stop`
discards every runtime position and the next Play re-materializes from authoring.

## Platformer baseline

The PlatformerController closes the first full gameplay loop on top of the AABB
resolver: side-view movement with gravity, a grounded state and a jump.

Authoring maps three values onto the canonical `PlatformerControllerComponent`
(the editor never forks the type): **Move Speed -> maxSpeed, Jump Speed ->
jumpForce, Gravity -> customGravity** (coyoteTime/jumpBuffer/climbSpeed keep their
defaults). `platformer_controller_commands` (Add/Remove + one
`SetPlatformerValueCommand` over a `PlatformerField` enum) edit it from the
Inspector with undo/redo; persistence stores the authored subset. Add starts at
180 / 420 / 1200, all values validated finite and `>= 0`.

`RuntimePlatformerController` adds the runtime-only `verticalVelocity` and
`grounded` — not persisted, recreated at Start, dropped at Stop. The per-frame
step (in `PlaySession::updatePlatformer`) is:

```text
jumpPressed && grounded -> verticalVelocity = -jumpSpeed; grounded = false  (edge, -Y up)
verticalVelocity += gravity * dt                                            (+Y down)
desired = { horizontalInput * moveSpeed * dt , verticalVelocity * dt }
KinematicMoveResult = moveKinematicEntity(desired)
  hitCeiling -> verticalVelocity = 0
  hitGround  -> grounded = true,  verticalVelocity = 0
  else       -> grounded = false
```

`moveKinematicEntity` now returns a `KinematicMoveResult`
(`appliedDelta` + `hitLeft/hitRight/hitCeiling/hitGround`) so grounded is derived
from the actual downward contact — not a new physics engine, just the result the
loop needs. Jump is edge-triggered: `RuntimeInputSnapshot.jumpPressed` is computed
by the application with `IsKeyPressed` (Space/W/Up); `PlaySession` never sees
Raylib. Because gravity must run every frame, `update` no longer early-returns on
empty input.

**Single movement writer.** A `RuntimeEntity.transform` has exactly one driver:
an object type may own only one of `TopDownController`, `PlatformerController`,
`LinearMover`. Each Add command rejects a second driver ("remove it first"). This
is also a **project invariant, not just a runtime convenience**: `ProjectValidator`
rejects a loaded file whose object type carries more than one driver, rather than
silently letting materialize choose. Materialize still applies a fixed priority
(Platformer > TopDown > LinearMover) as an internal defense, but a multi-driver
document is invalid and never loads — no priority/composition system as normal
semantics. Same purity as collisions: `Stop` restores the authoring position,
restart resets `verticalVelocity`/`grounded`, and the document/revision/dirty/
history never move.

## Add Instance baseline

ArtCade's core distinction is now complete in the UI: an **Object Type** is the
shared definition, an **Entity Instance** is a concrete, transformable presence in
a scene. Two Hierarchy buttons make it unambiguous:

```text
+ Entity   -> new ObjectTypeId + new EntityId   (independent object)
+ Instance -> existing ObjectTypeId + new EntityId   (shared components)
```

`+Instance` (`addInstanceOfSelectedType`) places another instance of the
**selected entity's** object type. (A free type picker is deferred: with the
current 1:1 type↔entity creation every type is named "Entity", so a dropdown
would be indistinguishable; selecting the source entity is the unambiguous choice
today.) It **reuses the existing `CreateEntityCommand`** — no second command, no
coordinator wrapper — then selects the new instance with `SelectEntityIntent`
(workspace state, not an undo entry):

```text
+Instance -> CreateEntityCommand(activeScene, newEntityId, selectedObjectTypeId, uniqueName)
          -> on success: SelectEntityIntent(newEntityId)
```

The new instance gets a fresh `EntityId` and its own Transform but keeps the
chosen `ObjectTypeId`; no `ObjectTypeDef` is duplicated and no component is
copied. Editing the type's collider/sprite/movement therefore updates every
instance — intentional prefab sharing. Undo removes only the instance (the type,
even if it was the last instance, is never touched); redo restores the same ids
and link. Edge cases: empty catalog disables it (no selection → no placeholder,
no `"Entity"` fallback, no first-type guess); during Play the coordinator rejects
the command and the button is disabled; a missing type or scene fails without
mutation. Deliberately out of scope: catalog drag-and-drop, a prefab browser,
instance counts, make-unique, instance→type conversion, generalized overrides,
deep duplication.

## RmlUi input commit baseline

Inspector text and number fields use RmlUi as a local edit buffer. Typing does
not create commands and does not mutate `ProjectDocument`.

```text
input/change
-> local control buffer only

Enter or blur
-> parse/validate/normalize
-> compare with authoritative value
-> typed Command only when valid and different

Escape
-> restore authoritative value
-> no Command
```

Incomplete or invalid values (`"-"`, `"."`, `"1e"`, `"nan"`, `"inf"`,
`"12px"`) do not change the revision, do not enter undo history, and do not
invalidate panels. `"12."` is accepted at commit as `12.0`.

## Viewport pick and drag baseline

The viewport's world<->screen mapping has a single source, `SceneViewCamera`
(`makeSceneViewCamera` + `screenToWorld`), shared by the renderer and picking so
a click maps to exactly what is drawn. The renderer builds its Raylib `Camera2D`
from it; picking inverts the same transform. `pickEntityAt` is a pure query on
`SceneFrameSnapshot` (sprite occludes placeholder, later draw order wins).

World-space drawing (grid, sprites, placeholders, colliders, selection) is
scissored to the **scene surface** — the world rectangle projected to screen,
intersected with the viewport — so an entity whose runtime position drifts
outside the scene (e.g. a LinearMover with no walls) is clipped at the scene
edge instead of painting over the panel backdrop. Out-of-bounds positions stay
legal in the domain (off-screen spawns, side-entering enemies); only rendering is
clipped. The scene-name chip is a viewport-space overlay drawn after, outside the
scene scissor.

Selection and move follow the existing command/intent split — no new authority,
no command per mouse move:

```text
left press in viewport
-> screenToWorld -> pickEntityAt
-> SelectEntityIntent (INVALID clears selection)
-> capture start mouse-world + entity authoring position   (local drag state)

drag
-> local preview only: the draw path offsets the dragged entity in its snapshot
   (placeholder, sprite AND collider overlay, so they move together)
-> no command, no revision, no invalidation

left release
-> one SetEntityPositionCommand(start position + world delta)  (zero delta: none)
```

The drag state is transient presentation owned by the application; it never
enters `ProjectDocument`. Pick + drag is Edit-mode only; Play keeps its own input
path. `pickEntityAt` and `screenToWorld` are unit-tested in `editor-core`.

## Scene view navigation baseline

The Scene View navigates like a level editor — pan and zoom, no scrollbars (which
would create a second, parallel representation of the camera that fights zoom,
negative coordinates and out-of-bounds entities). RmlUi owns only the viewport
hole; ArtCade's `Camera2D` (via the one `makeSceneViewCamera`) navigates the
world, and every flow — rendering, world↔screen, picking, drag, scene clipping,
grid, collider overlay — shares it. No second scroll offset.

Per-scene camera lives in workspace state, `EditorState.sceneViews[sceneId]`
(`{pan, zoom}`): not persisted, never dirties, not in undo/redo, distinct per
scene. Inputs:

```text
wheel              -> zoom under the cursor (the world point stays put), 10%–800%
middle-drag        -> pan
Space + left-drag  -> pan (right button stays free for context menu / Create Here)
Fit View to Bounds -> centre + zoom-to-fit (Scene Inspector button)
toolbar zoom %     -> click resets to 100% (target unchanged)
```

Zoom-under-cursor and Fit are orchestrated by the application (they need the
viewport pixel rect) but only ever apply the existing `SetViewportZoomIntent` +
`PanViewportIntent` — workspace intents, `Viewport` invalidation only, no command,
no `DomainChange`, no dirty. Zoom-under-cursor reads the world point under the
mouse before the zoom, applies the zoom, then pans by the difference so that point
stays fixed. Pan only runs when the cursor is over the viewport, no text field is
focused, and no entity drag is in progress; Space+left is a pan gesture, so the
pick path ignores a left-press while Space is held.

A scene is **auto-fit once**, the first time it is seen active in Edit mode. The
`initialized` flag lives in the scene's view state (`EditorSceneViewState`), not an
app-side registry, so it shares the single `sceneViews` lifecycle — cleared by
`replaceProject`, pruned with a deleted scene, reset for a fresh one — with no
second tracker to reconcile. The app marks it only after a real fit (so a
not-yet-laid-out viewport retries next frame). It never re-fits on selection,
window resize, scene resize, or returning from Play —
that would make the view jump; "lost the view" is recovered explicitly with Fit
View. Pan is free (no hard clamp to bounds) so off-screen spawns and side margins
stay visible; Fit View is the recovery. No scrollbars, minimap, navigator, pan
inertia, animated zoom or `F`-to-frame (deprecated).

## Undo / Redo baseline

`CommandStack` owns an undo and a redo stack of `CommandEntry{command,
revisionBefore, revisionAfter}`. A new command records onto undo and discards the
redo branch. Undo runs `command->undo`, redo re-runs the same `command->apply`
(no inverse is built, no UI re-read) — the existing commands are reusable because
each captures its previous value once and keeps its next value.

```text
toolbar Undo / Ctrl+Z          -> EditorCoordinator::undo  -> restoreRevision(before)
toolbar Redo / Ctrl+Y/Shift+Z  -> EditorCoordinator::redo  -> restoreRevision(after)
```

Dirty stays correct across the walk because revisions are stable ids, not a
counter bumped per mutation: `markDirty` allocates from a monotonic high-water
mark, and undo/redo *restore* the entry's recorded id. So a redo back to the
saved revision reports clean, and a command executed after an undo gets a fresh
id that cannot collide with the discarded branch. `replaceProject` clears both
stacks; Save updates `savedRevision` only. Both ops are coordinator-guarded
during Play (rejected, console warning, no authoring mutation); the disabled
buttons are affordance only. Single entry points, no transaction manager, no
history dropdown, no command grouping.

## Image import + Assets panel baseline

The native editor imports its own images, so it no longer depends on assets
staged by hand. There is **one canonical import entry point**; every UI source
converges on it, with no per-UI import path:

```text
Assets panel ─┐
File > Import ─┤ (future)
Drag & Drop ──┼─> importAsset(coordinator, projectRoot, {kind, sourcePath})
Inspector ────┘ (future)
```

`importAsset` (in `asset_import`) owns the common pipeline — reject during Play,
require a saved project, validate the source, choose a portable unique
destination, copy, run the per-kind command, roll the copy back on failure — then
switches on `AssetKind` to a typed command. The single entry point is about the
*operation*; the per-kind domain stays typed:

```text
importAsset(Image) -> assets/images/<unique>.<ext> -> AddImageAssetCommand
importAsset(Audio) -> assets/audio/<unique>.<ext>  -> AddAudioAssetCommand (load mode)
importAsset(Font)  -> assets/fonts/<unique>.<ext>  -> AddFontAssetCommand  (pixel size, glyph preset)
   (the copied file is rolled back if the command fails)
```

Supported now: image `png/jpg/jpeg/webp`, audio `wav/ogg/mp3`, font `ttf/otf`.
Audio load mode defaults by extension (wav -> StaticSound, else Stream) and can be
overridden in the request. The UI trigger (Assets panel "Import Image/Audio/Font")
only picks the file and calls `importAsset` with the kind. `ProjectDocument` only
gets `AssetId` + relative `sourcePath` (never absolute); one suffix keeps the file
name and `AssetId` unique together.

Import, use and remove are distinct operations: image assignment reuses
`set-sprite-asset` (`SetSpriteRendererAssetCommand`), removal is the typed
`Remove{Image,Audio,Font}AssetCommand` and **does not delete the file on disk**
(orphan cleanup is separate). Undo/redo and save/reload cover all three catalogs.
Audio/font are catalog-only for now — their consumers (audio playback, font
rasterisation/preview) are deferred to dedicated slices; the typed model carries
the fields those consumers will read (`loadMode`, `defaultPixelSize`,
`glyphPreset`).

Textures resolve against the **project root** (`currentProjectPath.parent_path()`)
for a loaded project; with no project open yet the root is the executable
resources (an empty catalog, nothing to resolve). The renderer still consumes
only `SceneFrameSnapshot` + `TextureCache`;
`TextureCache::invalidate(assetId)` is available for a future catalog change that
keeps the same id.

## Unsaved-changes guard baseline

Now that the editor makes real, persistable edits, a destructive action must not
silently lose work. The guard wraps New Project, Open Project and Exit:

```text
destructive action requested
-> document.isDirty()?
   no  -> run immediately
   yes -> native confirm: Save / Discard / Cancel
          Save    -> atomic save; run only if it succeeds
          Discard -> run, dropping changes
          Cancel  -> change nothing
```

The decision is a pure, unit-tested function, `resolveUnsavedGuard(dirty, choice,
saveSucceeded)`; the confirm itself is a blocking native dialog, so no pending
state machine, modal manager, event bus, or dirty polling is introduced. The
"which action was requested" is implicit in the synchronous call site, not stored
in `ProjectDocument` or `EditorUiState`.

A failed Save keeps the project loaded and dirty and aborts the action. On Exit,
Cancel clears the platform close flag and keeps the app running. Open/New are
rejected outright during Play ("Stop Play before opening another project") with
no hidden auto-stop; Exit may still run the guard and then terminate.

## New project baseline

New Project closes the native project lifecycle (`New -> Edit -> Save/Save As ->
Open -> guard -> Exit`). There is one application entry point, the File > New
handler, which reuses the existing pieces rather than adding a lifecycle system:

```text
New requested
-> isPlaying()?  yes -> reject ("Stop Play before creating a new project"), no auto-stop
-> resolveUnsavedGuard (same Save/Discard/Cancel as Open/Exit)
   Abort -> nothing changes
-> coordinator.replaceProject(ProjectDocument{ProjectDoc{}})   // empty, valid
-> textureCache.clear()        // explicit app path consuming ProjectReplaced
-> currentProjectPath.clear()  // no destination yet
-> window title -> "Untitled"
```

The new document is genuinely empty: 0 scenes, 0 entities, 0 assets, empty
`startSceneId`. No wizard, template, or auto-created scene — the first
`CreateSceneCommand` applies the existing start-scene invariant. `replaceProject`
already provides the rest of the contract (rejected during Play, clean via
high-water `replaceClean`, normalized empty active scene, cleared selection,
pruned per-scene view state, cleared undo/redo) while preserving `EditorUiState`
(layout/filters). Two concepts stay distinct and are deliberately not merged:

```text
dirty           -> content differs from the last baseline
hasProjectPath  -> a destination exists on disk
```

So a fresh project is **clean but path-less**: nothing to lose yet, but Save must
route through Save As until the first save. The "which action was requested" stays
implicit in the synchronous call site; no pending state is stored. The empty-state
transition is unit-tested in `editor-core`; the guard combinations and the
during-Play rejection are covered by the existing `resolveUnsavedGuard` and
`replaceProject`-during-Play tests. Path clearing and the title are application
state, verified by smoke.

## Console copy baseline

Sharing a full error matters more than free text selection, so the first slice is
deliberately small: click a Console row to select it, then copy the whole message
with the Copy button or Ctrl+C. No per-character selection, no single textarea, no
context menu, no `.log` export.

```text
click a Console row -> ConsolePanel.selected_ (local UI state)
Copy button / Ctrl+C -> copySelectedConsoleMessage()
                     -> coordinator.consoleMessage(index)  (nullptr-safe)
                     -> formatConsoleMessageForClipboard()  (pure, editor-core)
                     -> raylib SetClipboardText
```

The selection is purely local panel state (`std::optional<std::size_t>` into the
coordinator's full log) — not a Command, Intent, `EditorState`, `ProjectDocument`,
event bus, clipboard service or generic selection system. It is clamped/reset on
every refresh, so a shrunk log or replaced project can never leave a dangling
selection or copyable ghost. The clipboard text comes from the **model** message,
not the rendered row, so it is the full untruncated text, prefixed with the level
(`[Error] ...`). `formatConsoleMessageForClipboard` is pure and unit-tested; only
the `SetClipboardText` call lives in the native UI layer.

Ctrl+C precedence: a focused RmlUi text field keeps its own copy (the editor's
shortcut is gated by `textFocus`, same as Undo/Redo); otherwise Ctrl+C copies the
selected message, and with nothing selected it is a no-op. The Copy button is
disabled whenever there is no selection.

## Inspector layout baseline

The Inspector shows **only the components actually present** on the selected
entity. Absent components are no longer pre-listed as empty sections; they are
added from a single **Add Component** affordance. For a bare entity the panel is
just `Identity`, `Transform` and the Add Component button — not five "Add X"
blocks.

Each component section header is compact: `icon NAME  [ownership badge]  [x]`.
The badge states the real ownership (not decoration) and replaces the repeated
"Scope: Shared by object type" rows:

```text
Transform            -> INSTANCE   (structural: no remove)
Box/Linear/TopDown/Platformer -> TYPE
Sprite Renderer      -> OVERRIDE (instance) | INHERITED (from the type)
```

Remove is a small `x` in the header that calls the existing `Remove*Command`
(undo covers it, so no confirm dialog); Transform and an inherited sprite have no
`x`. The **Add Component** menu is an in-flow toggle (the only local panel state,
`addMenuOpen_`, reset on selection change or Play — no `EditorUiState`, no popup
positioning). It lists **only addable** components, and because the three
movement drivers are mutually exclusive it offers none once one is present — the
single-writer invariant made visible (the command guard stays authoritative). The
button disappears when nothing is addable.

This is presentation only: rendering from queries on Inspector invalidation, all
edits still through commands. While Play runs every control (fields, toggles,
remove, the Add menu) is disabled, consistent with the authoring freeze.
Collapsible sections were deliberately deferred — removing the phantom sections
already shortens the panel enough; folding can come if complex entities prove it
necessary.

## Scene Inspector baseline

Now that the scene's size is a concrete property consumed by clipping, spawn
centre, picking and the Outside-Scene UX, it needs an authoritative, editable
home. Rather than a new panel or a new `selectedSceneId`, the **Inspector has two
modes** keyed off the existing authority:

```text
entity selected            -> Entity Inspector
no entity + active scene   -> Scene Inspector   (activeSceneId, no new state)
no entity + no scene       -> "Select an entity"
```

Clicking a scene tab — or empty viewport space (pick returns INVALID, clearing the
selection) — shows the Scene Inspector with no extra intent. Three concepts stay
**deliberately separate** and must not be welded: **scene world bounds** (the
authoring extent, edited here), **game resolution** (logical output — Project
Settings, later) and **camera viewport** (runtime visible region — a camera
component, later). So the bounds section is "World Bounds", not "Resolution".

MVP+ sections:

```text
GENERAL       Name (Command) · ID (read-only) · Start (Set as Start / marker) · Entities
WORLD BOUNDS  Width · Height (whole pixels, shown in `wu`) · Fit View to Bounds
DIAGNOSTICS   Outside bounds (count of entities whose derived bounds leave the scene)
```

`Fit View to Bounds` is **workspace-only** — it recenters pan and zooms to fit via
intents (no command, no dirty); the viewport pixel rect is known only to the
application, so it runs through an app handler like the file/import triggers.
DIAGNOSTICS is a derived query recomputed on each Inspector refresh (no authority,
no cache); a non-zero count reads as a soft warning. Empty states are explicit:
**no scene → "No scene open"**, scene + no entity → Scene Inspector, entity →
Entity Inspector.

Edits go through commands like everything else: `RenameSceneCommand`
(Hierarchy|Inspector|Viewport|Toolbar) and the new `SetSceneSizeCommand`
(Inspector|Viewport); Set as Start reuses `SetStartSceneCommand`. `SetSceneSize`
validates width/height finite and > 0 and normalizes to whole pixels at commit
(same buffer→Enter/blur→validate→compare→command path as the numeric fields).
**Resizing never moves instances** — an entity left outside the new bounds keeps
its coordinates (the Outside-Scene UX flags it; no clamp, no hidden correction).
`name` and `worldSize` already round-trip through the serializer, so persistence
needed no change. Scene-tab selection stays workspace-only (no dirty/undo).

Deferred to their own slices (not added here without a consumer): scene layers
(persistent order + entity membership, with the editor-only hidden/locked flags
living in `EditorState.sceneViews`), and global/runtime scene properties (gravity,
camera, music, ambient, background) which arrive with the capability that uses
them. Collapsible sections likewise wait until complexity demands them.

## Scene layers baseline

A scene layer is a small, precise concept: it belongs to a scene, determines
organization and render order of instances, and nothing else (not collision,
physics, logic, camera or asset folders — `Scene Layer != Collision Layer`). The
guiding rule is one authority per datum, one entry point per operation, a renderer
that receives a ready projection.

```text
SceneDef.layers          -> the SINGLE order authority (index 0 = background, last = foreground)
SceneDef.defaultLayerId  -> persistent fallback; every scene always has a real Default layer
EntityInstance.layerId   -> the SINGLE membership authority
EditorSceneViewState     -> activeLayerId + hiddenLayerIds (workspace; never persisted/dirty)
```

There is no `int order` / `zIndex` beside the vector, no membership copy in the
object type or a parallel map, and no `LayerManager` — responsibilities stay in
ProjectDocument (data) / commands (persistent mutation) / intents (workspace) /
the snapshot collector (order + filter) / the renderer (draw).

Persistent mutations are typed commands (dirty + undo): `AddSceneLayerCommand`
(unique id + name), `RenameSceneLayerCommand`, `MoveSceneLayerCommand` (reorders
in the vector — no order field), `RemoveSceneLayerCommand` (the **Default layer is
never removable, a non-empty layer is rejected** — no implicit entity move), and
`SetEntityLayerCommand`. Creation threads an explicit layer: `+Entity` /
`+Instance` / `Create Here` read the workspace active layer and pass it to the
command, which only validates the layer exists in the scene (it never deduces the
layer itself). Active layer and editor visibility are workspace intents
(`SetActiveLayerIntent`, `ToggleLayerEditorVisibilityIntent`) — no dirty, no undo;
`reconcileWorkspace` repoints a vanished active layer to the default and drops
vanished hidden ids after a remove / project replace / scene change.

`createScene` always makes a real `Default` layer; legacy files with no layers are
migrated at load (a Default is created and every instance assigned to it) — no
`""`/`"default"` fallback survives past load, the same discipline as the
objectType fix. The renderer stays passive: the Edit `collectSceneFrameSnapshot`
emits sprites/colliders already in layer order (back-to-front), skipping
`hiddenLayers`; picking reverse-iterates the same snapshot, so render and pick
share one order with no parallel sort. Editor visibility is **never** copied into
the `PlaySession` — a layer hidden in Edit still renders in Play (and a separate
persistent `runtimeVisible` is deliberately deferred so the two never conflate).

Scene Inspector shows a LAYERS list (active marker, eye toggle, up/down reorder,
remove on non-default, Add Layer; rows reversed so the foreground is on top); the
Entity Inspector shows a layer picker (`SetEntityLayerCommand`). Deferred until a
concrete need: layer lock, runtime visibility, drag-and-drop reorder, per-entity
z-index within a layer, parallax, and a rename-from-UI affordance.

## Feature Template

Every migrated feature must fill this before implementation:

```text
Feature:
Source of truth:
Command or Intent:
Validation:
DomainChange:
EditorInvalidation:
Runtime effect:
Undo:
Persistence:
Test:
Old path removed:
```

If this cannot be filled linearly, simplify the feature before porting it.

## Persisted Schema Boundary

Current loading is intentionally narrow:

```text
filesystem bytes
-> ProjectSerializer::deserialize()
-> ProjectMigration::migrate()
-> ProjectValidator::validate()
-> EditorCoordinator::replaceProject()
```

Before the first real persisted schema change, this boundary must evolve in one
of two ways:

- parse JSON into a temporary persisted representation, migrate that shape, then
  build the current `ProjectDocument`;
- or keep version-specific parsers inside `ProjectSerializer`.

The filesystem layer must remain an adapter only. It reads/writes bytes and must
not learn about `EditorState`, `EditorUiState`, RmlUi, invalidation, undo, or
runtime projection.
