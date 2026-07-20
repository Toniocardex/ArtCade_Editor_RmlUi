# ArtCade RmlUi — RU-02 GameplaySession Refactor Plan (congelato)

**Stato:** congelato — piano approvato, pre-implementazione
**Ambito:** runtime condiviso (`ArtCade-Studio_V2/runtime-cpp`) — `Application`/`game.exe`/WASM — e, a valle in RU-03, l'editor (`ArtCade_Editor_RmlUi`)
**Scopo:** dettaglio implementativo congelato per RU-02 (estrazione di `GameplaySession` dal runtime reale) e per l'integrazione RU-03 con Editor Play, in sostituzione della stesura sintetica di RU-02/RU-03 nel roadmap principale
**Baseline:** decisione architetturale congelata dall'utente, 2026-07-20
**Ultimo aggiornamento:** 2026-07-20

## 1. Autorità e precedenza

Questo documento è subordinato, nell'ordine, a:

1. `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`, `ARTCADE_RMLUI_ARCHITECTURE.md`, `ARTCADE_RMLUI_ENGINEERING_GATES.md`, `RMLUI_MIGRATION_CONTRACT.md` (come da `PLAY_RUNTIME_UNIFICATION_ROADMAP.md` §1);
2. `PLAY_RUNTIME_UNIFICATION_ROADMAP.md` — questo documento è il dettaglio congelato della sola slice RU-02 (e della sua integrazione RU-03), non lo sostituisce.

Le decisioni architetturali qui contenute sono **congelate**: ogni deviazione in fase di implementazione richiede una revisione esplicita del piano, non un'interpretazione libera. Dove questo documento e la sezione RU-02/RU-03 del roadmap principale divergono, questo documento prevale per il dettaglio implementativo; il roadmap principale resta autorevole per lo stato sintetico e le dipendenze tra slice.

## 2. Decisione architetturale

`GameplaySession` non deve essere un nuovo runtime costruito accanto ad `Application`. Deve essere la porzione di simulazione gameplay **estratta** dal runtime reale attualmente posseduto da `Application`, che diventa essa stessa utilizzatrice di `GameplaySession`.

Architettura finale:

```
┌──────────────────────────────────────────────────────────────┐
│                        HOST                                  │
│                                                              │
│  Application / EditorHost / WebHost                          │
│  ├── finestra e main loop                                    │
│  ├── polling input di piattaforma                            │
│  ├── renderer e presentazione                                │
│  ├── risorse GPU/audio device                                │
│  ├── EditorAPI / bridge WASM                                 │
│  ├── splash e policy prodotto                                │
│  └── profiler publishing                                     │
│                         │                                    │
│                         ▼                                    │
│                  GameplaySession                             │
│  ├── World                                                   │
│  ├── scene lifecycle                                         │
│  ├── entity gateway                                          │
│  ├── physics                                                 │
│  ├── Logic Runtime                                           │
│  ├── Script Runtime                                          │
│  ├── GameAPI / LuaHost                                       │
│  ├── animation, state, timers, tween                          │
│  ├── spawn/destroy                                           │
│  └── unico fixed simulation tick                             │
└──────────────────────────────────────────────────────────────┘
```

Il runtime attuale conferma che questa separazione è praticabile: `tickFixedStep()` contiene quasi esclusivamente simulazione; il rendering effettivo viene eseguito successivamente da `tickFrameEnd()` e `renderActiveScene()`. L'unica chiamata renderer presente nel fixed tick è `clearDrawQueue()`, che appartiene alla preparazione host del frame.

La parte complessa è il composition root: `initSubsystems()` costruisce e collega `World`, gateway, scene lifecycle, Logic, Script, GameAPI, LuaHost, renderer, dialog e bridge EditorAPI attraverso lo stesso `EngineContext`. Il wiring deve quindi essere trasferito progressivamente, mai riscritto in parallelo.

## 3. Clausole progettuali non negoziabili

### 3.1 Un solo runtime gameplay

Deve esistere una sola implementazione di: fixed tick; movimento; fisica; collisioni; spawn e destroy; scene lifecycle; Logic Runtime; Script Runtime; animazioni; variabili runtime; dispatch degli eventi.

```
game.exe ───────────────┐
                        ├── GameplaySession
Editor Play RmlUi ──────┤
WASM ───────────────────┘
```

Non è accettabile: `Application` runtime + `EditorNative::PlaySession` + `GameplaySession`, con implementazioni simili mantenute allineate mediante test.

### 3.2 Un solo fixed tick

Alla fine deve esistere esclusivamente:

```cpp
GameplaySession::tickFixedStep(float dt);
```

`Application` può chiamarlo, ma non deve conservarne una seconda implementazione.

Corretto:

```cpp
while (accumulator_ >= targetDt_) {
    gameplaySession_->tickFixedStep(targetDt_);
    accumulator_ -= targetDt_;
}
```

Non corretto: `Application::tickFixedStep(...)` e `GameplaySession::tickFixedStep(...)` con corpi duplicati o parzialmente sovrapposti.

### 3.3 Un solo composition root gameplay

La costruzione di `SceneManager`, `SceneMutationService`, `SceneLifecycleService`, `RuntimeEntityGateway`, `World`, `RuntimeLogicHostAdapter`, `LogicRuntime`, `ScriptRuntime`, `GameAPI`, `LuaHost` deve avvenire in un unico punto. Durante la migrazione il wiring viene prima utilizzato per riferimento e successivamente trasferito. Non deve essere copiato in una nuova sequenza "equivalente".

### 3.4 Autorità separate per ciclo di vita

| Dominio | Unica autorità |
|---|---|
| Authoring persistente | `ProjectDocument` |
| Simulazione | `GameplaySession` |
| Stato workspace editor | `EditorState` |
| Host/window/platform | `Application` o altro host |
| Frame renderizzabile | snapshot immutabile |
| Risorse device/GPU | servizi host |
| UI editoriale | RmlUi |

Flusso unidirezionale:

```
ProjectDocument
→ RuntimeProjectSnapshot
→ GameplaySession
→ GameplayFrameSnapshot
→ Renderer
```

Vietati: `GameplaySession → ProjectDocument`; `Renderer → GameplaySession` mutation; UI model ↔ runtime model sync continuo.

### 3.5 Nessuna sincronizzazione nascosta

Una sessione viene materializzata una volta: `Start Play → validazione → snapshot → GameplaySession`.

Durante Play: nessun polling del `ProjectDocument`; nessun hot sync implicito; nessun write-back; nessuna lettura authoring per frame; nessuna modifica runtime riflessa nel documento.

Una modifica authoring richiede: `Stop → nuova materializzazione → nuova GameplaySession`.

### 3.6 Logic e Script convergono sullo stesso host

Il `RuntimeLogicHostAdapter` esistente collega già Logic a World, gateway, Audio, variabili, input e fisica, incluso lo spawn reale degli Object Type. Deve diventare interno alla sessione:

```
Logic Runtime ──┐
                ├── Runtime gameplay host → World/Gateway
Script Runtime ─┘
```

Nessun host semplificato per l'editor.

### 3.7 GameplaySession non renderizza

La sessione non deve: aprire finestre; chiamare Begin/End Drawing; svuotare la draw queue; presentare il backbuffer; gestire fullscreen; gestire RmlUi; possedere `EditorViewportService`; chiamare `EditorAPI`.

Può produrre esclusivamente una proiezione immutabile destinata al renderer.

### 3.8 Nessun polling input dentro GameplaySession

Il polling appartiene all'host.

```
Raylib / Web / test input
→ GameplayInputFrame
→ GameplaySession
```

Questo consente allo stesso runtime di essere usato da: game nativo; WASM; editor RmlUi; test headless; replay deterministici futuri.

### 3.9 Nessun secondo EngineContext

Durante il refactor deve continuare a esistere un solo grafo di servizi. Non creare `Application::ctx_` e `GameplaySession::ctx_` contenenti puntatori differenti agli stessi domini.

Migrazione consigliata: fase transitoria → un solo `EngineContext` esistente, passato per view/reference; fase finale → `GameplayContext` ristretto + `HostServicePorts` espliciti. Non sostituire il context attuale con un secondo service locator equivalente.

### 3.10 Ordine del tick congelato

L'ordine attuale è parte del contratto runtime, non un dettaglio implementativo. In particolare, il codice garantisce che gli Script manuali vengano aggiornati dopo Logic e prima dell'integrazione Platformer, permettendo intenzionalmente allo Script di prevalere sugli intent generati dalla Logic Board. Qualunque cambiamento dell'ordine richiede: decisione esplicita; documentazione; test; valutazione di compatibilità.

## 4. Confine finale dei moduli

### 4.1 Host-owned

Restano fuori dalla sessione: Renderer; Window; Presentation surface; Input device/polling; TextureManager; EditorViewportService; EditorAPI bridge; WASM browser bridge; Splash/watermark; Frame accumulator; Frame-end publishing; Console bridge.

`AssetLoader`, `Audio` e `Dialog` richiedono un trattamento più preciso perché contengono sia aspetti platform sia servizi consumati dal gameplay (vedi D-14, D-15).

### 4.2 Gameplay-owned

La destinazione finale è che `GameplaySession` possieda: `EventBus`; `TimeManager`; `VariableManager`; `TweenManager`; `SpriteAnimator`; `CameraManager`; `GameStateManager`; SaveLoad gameplay state; `Physics`; `SceneManager`; `SceneMutationService`; `SceneLifecycleService`; `RuntimeEntityGateway`; `World`; `RuntimeLogicHostAdapter`; `LogicRuntime`; `ScriptRuntime`; `GameAPI`; `LuaHost`; Logic scopes; Script programs/attachments; Collision transition state; Runtime entity queues.

L'attuale `Application::Modules` miscela tutti questi oggetti insieme ai moduli host.

### 4.3 Servizi host esposti tramite porte

I moduli gameplay non devono dipendere direttamente da `Application`. Interfacce indicative:

```cpp
class IGameplayAudioService {
public:
    virtual ~IGameplayAudioService() = default;
    virtual bool playSound(const AssetId& assetId, float volume) = 0;
    virtual void update() = 0;
};

class IGameplayDialogGate {
public:
    virtual ~IGameplayDialogGate() = default;
    virtual bool blocksGameplay() const = 0;
    virtual void tick(float dt) = 0;
};

class IRuntimeAssetResolver {
public:
    virtual ~IRuntimeAssetResolver() = default;
    virtual std::string resolveImage(const AssetId&) const = 0;
    virtual std::string resolveAudio(const AssetId&) const = 0;
    virtual std::string resolveFont(const AssetId&) const = 0;
};

class IRuntimeProfilerSink {
public:
    virtual ~IRuntimeProfilerSink() = default;
    virtual void addGameplayMs(double) = 0;
    virtual void addPhysicsMs(double) = 0;
    virtual void addLuaMs(double) = 0;
};
```

Le porte devono essere strette. Non passare l'intera `Application` o il renderer completo come scorciatoia permanente.

## 5. API finale di GameplaySession

Forma indicativa:

```cpp
struct GameplayInputFrame {
    std::vector<LogicKey> pressed;
    std::vector<LogicKey> released;
    std::vector<LogicKey> held;
    Vec2 pointerPosition{};
    std::uint32_t pointerButtonsDown = 0;
    std::uint32_t pointerButtonsPressed = 0;
};

struct GameplayHostServices {
    IGameplayAudioService* audio = nullptr;
    IGameplayDialogGate* dialog = nullptr;
    IRuntimeAssetResolver* assets = nullptr;
    IRuntimeProfilerSink* profiler = nullptr;
    IRuntimePresentationCommands* presentation = nullptr;
    IRuntimeStorageService* storage = nullptr;
};

struct GameplaySessionConfig {
    GameplayHostServices host;
    PhysicsMode physicsMode = PhysicsMode::Auto;
    float fixedDeltaSeconds = 1.0f / 60.0f;
};

class GameplaySession {
public:
    static Result<std::unique_ptr<GameplaySession>> create(const GameplaySessionConfig& config);

    Result<void> loadProject(const RuntimeProjectSnapshot& snapshot);
    Result<void> startProject();
    Result<void> startScene(const SceneId&);
    Result<void> restartScene();

    void dispatchInput(const GameplayInputFrame& input);
    void tickFixedStep(float dt);

    GameplayFrameSnapshot buildFrameSnapshot() const;
    std::vector<RuntimeDiagnostic> drainDiagnostics();

    Result<void> stop();
    bool running() const;
    const SceneId& activeSceneId() const;
};
```

La sessione non deve esporre pubblicamente: `World*`; `RuntimeEntityGateway*`; `ProjectDocument*`; `Renderer*`; `EditorAPI`; moduli interni; container ECS mutabili.

Durante la migrazione possono esistere accessi interni temporanei, ma non devono diventare API pubblica stabile.

## 6. Piano a sotto-fasi

### RU-02a — Characterization e dependency map

**Obiettivo**: congelare comportamento e dipendenze prima del refactor. Nessuna modifica di ownership.

**Attività**

1. **Mappatura del composition root.** Documentare: ordine di costruzione; dipendenze obbligatorie; dipendenze opzionali; callback registrate; callback che catturano `Application`; ordine di shutdown; differenze native/WASM; servizi usati da `GameAPI`; servizi usati da `World`.
2. **Classificazione moduli.** Per ogni modulo: Host-owned / Gameplay-owned / Port host consumato dal gameplay / Transitorio-da eliminare.
3. **Congelamento tick order.** Documento o test con fasi numerate:

   Input frame: (1) Logic input dispatch, (2) Script input dispatch, (3) entity queue flush, (4) GameAPI input events.

   Fixed step: (1) Time/Tween/Animator/Camera/GameState, (2) deferred EventBus, (3) World gameplay systems, (4) scene transitions, (5) animation events, (6) Logic tick, (7) LuaHost tick, (8) Script update, (9) entity queue flush, (10) Dialog tick, (11) Platformer/controllers, (12) Physics, (13) entity queue flush, (14) physics → entities sync, (15) camera targets, (16) collision refresh, (17) lifecycle dispatch, (18) auto destroy, (19) entity queue flush, (20) lifecycle dispatch, (21) collision enter/exit, (22) Script diagnostics, (23) deferred EventBus, (24) Audio update.
4. **Test di caratterizzazione**: ordine chiamate tramite spy; Logic prima di Script; Script prima del Platformer; spawn installa scope; destroy cancella scope; scene transition richiama reset; dialog blocking sospende World/controller; animation events drained una volta; collision snapshot condiviso Logic/Script; shutdown inverso.

**Gate RU-02a**: zero variazioni funzionali; native build verde; WASM build verde; runtime test verdi; ordine del tick documentato; ownership map approvata.

**Stato gate (2026-07-20)**: [x] zero variazioni funzionali (solo documentazione + nuovo test, nessun file di `Application` toccato); [x] native build verde (49/49 CTest, incl. il nuovo `gameplay_tick_order_characterization_test`); [x] ordine del tick documentato (sotto); [x] ownership map (sotto); [!] **WASM build non verificato** — `emcc`/toolchain Emscripten non disponibile in questo ambiente di sviluppo; `build_wasm.bat` esiste ma non è stato eseguibile qui. Da chiudere su una macchina/CI con Emscripten prima di considerare RU-02a definitivamente `[x]`.

Test di caratterizzazione: [`tests/gameplay-tick-order-characterization-test.cpp`](../../../ArtCade-Studio_V2/runtime-cpp/tests/gameplay-tick-order-characterization-test.cpp) — copre Logic-prima-di-Script (etichettato onestamente come verifica di *contratto*, non ancora una vera guardia di regressione: il driver del test replica a mano la sequenza di `app_loop.cpp` perché `Application::tickFixedStep` è privato e non richiamabile da un test standalone; diventerà una vera guardia di regressione quando RU-02c esporrà `GameplaySession::tickFixedStep` e il driver chiamerà quello), install/cancel dello scope Logic su spawn/destroy reali, `ScriptRuntime::cancelOwner`, drain singolo degli eventi di `SpriteAnimator` — tutti verificati contro moduli di produzione reali (`World`, `RuntimeEntityGateway`, `LogicRuntime`, `ScriptRuntime`, `SpriteAnimator`), non contro doppi semplificati, salvo l'host (`RuntimeLogicHostAdapter` non è linkabile da un test standalone — vive in un header privato del target eseguibile `game` e richiederebbe un `Modules::Audio` reale mai testato headless in questo repo; il test usa un piccolo `Host` locale che inoltra le chiamate transform/platformer/animazione/variabili a `World`/`RuntimeEntityGateway`/`VariableManager` reali, coerente con la convenzione già in uso in `logic-board-test.cpp` per testare `LogicRuntime` in isolamento).

#### Risultati RU-02a (2026-07-20, verificato nel codice reale)

**Ordine di costruzione** (`app_bootstrap.cpp`):

`initUtilities()` (`app_bootstrap.cpp:29-61`): `EventBus → TimeManager → VariableManager → TweenManager → SpriteAnimator → CameraManager → SaveLoadManager` (init in quest'ordine), poi `GameStateManager` (dipende da `EventBus`, wired con `setEventBus` prima di `init()`).

`initSubsystems()` (`app_bootstrap.cpp:63-238`), ordine effettivo:

1. `EditorViewportService`, `Renderer` (window size 1280×720), `Physics`, `Input`, `Audio`, `AssetLoader` — costruiti insieme, init in sequenza.
2. `TextureManager` (init).
3. `SceneManager` (init).
4. `SceneMutationService(sceneManager)` — non ha `init()` proprio, costruita da reference.
5. `RuntimeEntityGateway(sceneManager)` (init) → `RuntimeLogicHostAdapter(gateway, audio)` (nessun `init()`, solo costruzione) → `LogicRuntime(logicHost)` (nessun `init()` visibile, costruzione diretta).
6. `SceneLifecycleService(sceneManager, sceneMutation, syncCallback)` — il callback cattura `gateway` per riferimento raw (`gw = entityGateway.get()`); poi `set_transition_handler([this]{ handleSceneTransition(...); })` cattura **`Application` stesso**; `entityGateway->set_scene_lifecycle_service(...)`.
7. `World(gateway, physics, variableManager)` — **unico costruttore obbligatorio a 3 dipendenze**; poi wiring: `entityGateway->setPhysics(physics)`, 4× setter su `logicHost` (World/VariableManager/Input/Physics) + `setSpawnInstaller([this]{ installLogicScopeForEntity(...); })` (cattura `Application`), `world->setSpriteAnimator(...)`, `world->setEntityDestroyedHandler([this]{...})` (cattura `Application`, cancella scope Logic + owner Script), `world->setRenderer(renderer)` (**opzionale**, `renderer_` di default `nullptr`), `sceneLifecycle->set_gameplay_reset_handler([this]{ world->onSceneActivated(); })` (cattura `Application`), `set_restore_handler([gw]{...})` (cattura gateway per riferimento), `world->setSceneLifecycleService(...)`.
8. `DialogManager` (init, `setContext(&ctx_)` — riceve l'intero `EngineContext`).
9. `GameAPI(ctx_)` (init) — riceve l'intero `EngineContext` per **reference permanente** (`const EngineContext& ctx_` come membro, non solo a `init()` — deroga esplicita alla regola scritta nel commento di `EngineContext` stesso, "Modules must NOT store the EngineContext beyond init()"). Registra 18 domini di binding Lua inclusi `camera.*` (via Renderer) e `dialog.*` — **GameAPI non è gameplay-puro**, ha bind reali verso Renderer/Dialog.
10. `LuaHost` — costruito, `registerBindings([&]{ gameAPI->registerAll(lua); })`, init.
11. `EditorAPI::wire*` (6 chiamate statiche: Engine/Lua/Renderer/EditorViewport/Dialog/SpriteAnimator) + `entityGateway->setSpriteAnimator(...)` + `EditorAPI::wireAudio/wireVariables/init(...)`.
12. Solo `#ifdef ARTCADE_WASM`: 6 `EditorAPI::set*Handler` che catturano `this` (Application) per bridge scene-mutation/invalidation/project-loaded/preview-restore/enter-play/exit-play.

**Dipendenze obbligatorie vs opzionali**: obbligatorie a runtime (mai null-checked nel tick) — `renderer`, `timeManager`, `tweenManager`, `spriteAnimator`, `cameraManager`, `gameStateManager`, `eventBus`, `world`, `entityGateway`, `gameAPI`, `luaHost`, `audio`. Opzionali (null-checked) — `dialogManager`, `logicRuntime`, `scriptRuntime`, `splash_`.

**Servizi usati da `GameAPI`** (da `game-api.h:20-36`): entity/object/pool, collision/physics, input, audio, text, state, debug, save, event (pure-Lua EventBus), time (pure-Lua), **camera (via Renderer)**, animation, lifecycle, grid, **shader**, component (linearMover/magnet/horde/autoDestroy/platformer), **dialog**. Conferma D-13: 3 dei 18 domini (camera/shader/dialog) sono binding verso servizi host/presentazione, non gameplay puro — non tutto in `GameAPI` è spostabile in blocco in `GameplaySession` senza prima separare questi binder per dominio.

**Servizi usati da `World`** (`world.h:42-53,156-158,213-216`): costruttore richiede `RuntimeEntityGateway&`, `Physics&`, `VariableManager&` (obbligatorie, per reference, mai nulle). Setter opzionali: `SceneLifecycleService*`, `Renderer*` (default `nullptr` — conferma che World **tollera già** l'assenza di renderer), `SpriteAnimator*`, più un callback `setEntityDestroyedHandler`.

**Callback che catturano `Application` (`[this]`)**: `sceneLifecycle->set_transition_handler` (→ `handleSceneTransition`), `logicHost->setSpawnInstaller` (→ `installLogicScopeForEntity`), `world->setEntityDestroyedHandler`, `sceneLifecycle->set_gameplay_reset_handler`, più le 6 `EditorAPI::set*Handler` WASM-only. Tutte e 5 le callback native devono essere ri-scritte per catturare `GameplaySession` in RU-02e (D-08); le 6 WASM restano host-side per costruzione (invocano bridge editoriale, non gameplay).

**Ordine di shutdown** (`app_bootstrap.cpp:240-287`, inverso rispetto alla costruzione ma non identico): `logicRuntime→scopes/objectTypes→logicHost` · `scriptRuntime→programs/attachments/collisionPairs` · `luaHost` · `gameAPI` · `dialogManager` · `world` · `entityGateway` (prima `set_scene_lifecycle_service(nullptr)`, poi `shutdown()`) · `sceneLifecycle` (prima `cancel_transition()`) · `sceneMutation` · `sceneManager` · `assetLoader` · `audio` · `input` · `physics` · `textureManager` · `renderer` · `gameStateManager` · `saveLoadManager` · `cameraManager` · `spriteAnimator` · `tweenManager` · `variableManager` · `timeManager` · `eventBus`. Nota: `entityGateway` viene distrutto **prima** di `sceneManager`/`sceneMutation` pur essendo stato costruito **dopo** `sceneManager` — non è un ordine puramente LIFO, dipende da chi referenzia chi (`gateway` tiene un riferimento a `sceneManager`, quindi deve morire per primo).

**Differenze native/WASM** (`app_loop.cpp:219-297`, `app_bootstrap.cpp:186-235`): (a) native controlla `renderer->shouldClose()` a ogni iterazione, WASM no (il loop è guidato da `emscripten_set_main_loop`); (b) native fa sempre `simulating = true`, WASM gate su `EditorAPI::s_mode == 1` (play/pause pilotato dall'editor browser) — **unica vera differenza comportamentale nel tick**, non solo di trasporto; (c) F11 fullscreen toggle solo native; (d) i 6 `EditorAPI::set*Handler` (scene-mutation/invalidation/project-loaded/preview-restore/enter-play/exit-play bridge) esistono solo `#ifdef ARTCADE_WASM`.

**Scoperta non anticipata dal piano originale — cadenza doppia di `CameraManager`**: `cameraManager->updateMotion(dt)` gira dentro `tickFixedStep` (fase 1 della sequenza a 24 passi, cadenza fixed-step), ma `cameraManager->trauma()/refreshShakeOffset(shakeDt)/decayTrauma(shakeDt)` girano in `loopIteration()` (`app_loop.cpp:290-294`), **fuori dal fixed tick**, una volta per frame reale (non per fixed-step), con `shakeDt = simulatedDt > 0 ? simulatedDt : frameTime` — cioè usa il tempo simulato se è girato almeno un fixed-step in quel frame, altrimenti il tempo reale del frame. Questo va preservato esattamente com'è: se lo shake finisse dentro `GameplaySession::tickFixedStep`, la cadenza cambierebbe (da "una volta a frame" a "una volta per fixed-step", potenzialmente più volte per frame quando l'accumulator ha arretrati) alterando il feel dello shake — una regressione comportamentale sottile che nessun test funzionale ovvio catturerebbe. **Decisione**: lo shake resta host-side (`Application`/futuro host) esattamente come oggi, chiamato dopo il blocco dei fixed-step in `loopIteration`, non spostato in `GameplaySession`.

**Sequenza input frame confermata** (`app_loop.cpp:248-280`), esattamente come da piano: (1) `logicRuntime->beginFrame()` + dispatch pressed/released/held per ogni `LogicKey` supportata, costruendo in parallelo lo `ScriptInputSnapshot`; (2) `scriptRuntime->dispatchInput(scriptInput)` — **stesso snapshot immutabile** condiviso tra Logic e Script, costruito nello stesso ciclo; (3) `world->flushEntityQueues()`; (4) `gameAPI->dispatchInputEvents()`, gated da `!dialogManager->isBlocking()`. Dispatchato **una sola volta per `loopIteration()`**, non per fixed-step — se l'accumulator fa girare più fixed-step nello stesso frame, tutti vedono lo stesso input già dispatchato prima del loro blocco.

**Classificazione moduli** (Host-owned / Gameplay-owned / Host-port consumato dal gameplay / Transitorio):

| Modulo | Classe | Note |
|---|---|---|
| Renderer, Window, TextureManager, EditorViewportService | Host-owned | Nessuna simulazione |
| Input | Host-owned | Polling raylib diretto, nessun seam oggi (D-07) |
| Audio | Host-owned, esposto come port | `IGameplayAudioService` (D-14) |
| AssetLoader | Host-owned | API solo file/zip-based |
| DialogManager | Host-owned, esposto come port | `IGameplayDialogGate` (D-15) |
| EventBus, TimeManager, VariableManager, TweenManager, SpriteAnimator, GameStateManager, SaveLoadManager | Gameplay-owned | Già headless-testabili singolarmente |
| CameraManager | Gameplay-owned, **con eccezione host-cadence** | `updateMotion` gameplay-owned; shake/trauma restano chiamate host-side (vedi sopra) |
| Physics, SceneManager, SceneMutationService, SceneLifecycleService, RuntimeEntityGateway, World | Gameplay-owned | Nucleo simulazione |
| RuntimeLogicHostAdapter, LogicRuntime, ScriptRuntime | Gameplay-owned | Già l'unico host condiviso Logic/Script |
| GameAPI, LuaHost | Gameplay-owned **con 3 domini host-facing** (camera/shader/dialog) | Non spostabile in blocco senza separare i binder (D-13) |
| `activeGameplayCollisionPairs`, logic scopes, script programs/attachments | Gameplay-owned | Oggi vivono in `Application::Modules` (D-05, D-09) |
| EditorAPI bridge (statico + 6 handler WASM) | Host-owned | Non deve mai entrare in `GameplaySession` (D-18, D-19) |

### RU-02b — Separare preparazione host e simulazione

**Obiettivo**: ripulire `tickFixedStep()` da ciò che non appartiene alla simulazione senza introdurre ancora `GameplaySession` owning.

**Spostamenti**: da `tickFixedStep()` all'host, `renderer->clearDrawQueue();`. Lo splash resta host-side (`splash_->update(dt);`); la sua temporizzazione può essere eseguita una volta per frame o sulla somma dei fixed step simulati, ma non deve diventare responsabilità di `GameplaySession`.

**Risultato**: il corpo residuo di `tickFixedStep()` deve rappresentare soltanto simulazione e servizi gameplay.

**Gate RU-02b**: output visivo invariato; splash invariato; profiler invariato; nessun draw nella futura area estraibile; test di ordine ancora verdi.

### RU-02c — GameplaySession non-owning

**Obiettivo**: spostare l'algoritmo gameplay senza cambiare ancora chi possiede i moduli.

**Struttura transitoria**:

```cpp
struct GameplayRuntimeRefs {
    World& world;
    Modules::Physics& physics;
    Modules::RuntimeEntityGateway& gateway;
    Modules::TimeManager& time;
    Modules::TweenManager& tweens;
    Modules::SpriteAnimator& animator;
    Modules::CameraManager& camera;
    Modules::GameStateManager& gameState;
    Modules::EventBus& events;
    Modules::GameAPI& gameApi;
    Modules::LuaHost& luaHost;
    Logic::LogicRuntime* logic = nullptr;
    Scripts::ScriptRuntime* scripts = nullptr;
    IGameplayAudioService* audio = nullptr;
    IGameplayDialogGate* dialog = nullptr;
    IRuntimeProfilerSink* profiler = nullptr;
};
```

`Application::Modules` continua temporaneamente a possedere tutto. `GameplaySession` usa reference non-owning.

**Codice da spostare, non copiare**: `dispatchGameplayCollisionTransitions();`; corpo gameplay di `tickFixedStep()`; diagnostiche Script post-step; stato `activeGameplayCollisionPairs`.

`Application` delega: `gameplaySession_->tickFixedStep(dt);`

**Gate RU-02c**: esiste una sola implementazione del fixed tick; `Application` non contiene più l'algoritmo gameplay; ownership invariata; `game.exe` usa già il delegato; WASM usa già il delegato; nessun nuovo composition root.

### RU-02d — Confine input immutabile

**Obiettivo**: rimuovere dalla simulazione la dipendenza dal polling Raylib.

**Host**: `Application` continua a: `Input::poll`; F11/fullscreen; comandi host; costruzione `GameplayInputFrame`.

**Sessione**: `gameplaySession_->dispatchInput(inputFrame);`. Responsabilità: `LogicRuntime::beginFrame`; dispatch pressed/released/held; dispatch stesso snapshot a Script; flush entity queue; dispatch GameAPI input events se non bloccato. Lo snapshot deve essere immutabile e condiviso tra Logic e Script.

**Reset edge flags**: `Input::resetFrameState()` resta host-side dopo il frame rendering. Non va chiamato dalla sessione.

**Gate RU-02d**: nessun `Input::poll()` nella sessione; nessun Raylib key lookup nella sessione; Logic e Script ricevono lo stesso frame; test input deterministici; supporto a input sintetico headless.

### RU-02e — Estrarre il composition root gameplay

**Obiettivo**: trasferire il wiring reale da `Application::initSubsystems()` a `GameplaySession::initialize()`.

**Regola fondamentale**: le righe esistenti devono essere spostate per gruppi mantenendo ordine, callback, lifetime, gestione errori, nomi dei boot step. Non scrivere una nuova inizializzazione basata soltanto sulla comprensione concettuale.

**Sequenza gameplay finale**:

```
Utilities gameplay
→ EventBus → TimeManager → VariableManager → TweenManager
→ SpriteAnimator → CameraManager → SaveLoadManager → GameStateManager

Simulation graph
→ Physics → SceneManager → SceneMutationService → RuntimeEntityGateway
→ RuntimeLogicHostAdapter → SceneLifecycleService → World
→ LogicRuntime → GameAPI → LuaHost → ScriptRuntime
```

**Wiring da preservare**: gateway → Physics; Logic host → World; Logic host → variables; Logic host → input query; Logic host → Physics; spawn installer; World → SpriteAnimator; World destroy handler; lifecycle activation handler; restore handler; scene transition handler; GameAPI binding; LuaHost registration; Script/Logic installation.

**Callback**: non devono più catturare `Application` per modificare oggetti posseduti dalla sessione.

Corretto: `[this](EntityId id) { return installLogicScopeForEntity(id); }` dove `this` è `GameplaySession`. L'host riceve soltanto eventi: `config.onSceneTransition(result);`, `config.onRuntimeInvalidation(flags);`.

**Gate RU-02e**: World/gateway/Logic/Script costruiti soltanto dalla sessione; `Application::initSubsystems()` costruisce solo host e avvia la sessione; una sola registrazione GameAPI; un solo LuaHost; una sola istanza Logic Runtime; una sola installazione scope; `game.exe` e WASM sempre verdi.

### RU-02f — Trasferimento ownership e shutdown

**Obiettivo**: dividere definitivamente `Application::Modules`.

**Forma finale indicativa**:

```cpp
struct Application::HostModules {
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Input> input;
    std::unique_ptr<AudioBackend> audio;
    std::unique_ptr<TextureManager> textures;
    std::unique_ptr<AssetLoader> assets;
    std::unique_ptr<DialogManager> dialog;
    std::unique_ptr<EditorViewportService> editorViewport;
};

class GameplaySession {
    struct Modules {
        std::unique_ptr<EventBus> events;
        std::unique_ptr<TimeManager> time;
        std::unique_ptr<VariableManager> variables;
        std::unique_ptr<TweenManager> tweens;
        std::unique_ptr<SpriteAnimator> animator;
        std::unique_ptr<CameraManager> camera;
        std::unique_ptr<GameStateManager> gameState;
        std::unique_ptr<SaveLoadManager> saveLoad;
        std::unique_ptr<Physics> physics;
        std::unique_ptr<SceneManager> scenes;
        std::unique_ptr<SceneMutationService> sceneMutation;
        std::unique_ptr<SceneLifecycleService> sceneLifecycle;
        std::unique_ptr<RuntimeEntityGateway> gateway;
        std::unique_ptr<World> world;
        std::unique_ptr<RuntimeLogicHostAdapter> logicHost;
        std::unique_ptr<LogicRuntime> logic;
        std::unique_ptr<ScriptRuntime> scripts;
        std::unique_ptr<GameAPI> gameApi;
        std::unique_ptr<LuaHost> luaHost;
    };
};
```

**Shutdown**: mantenere l'ordine inverso delle dipendenze. Prima dello shutdown: annullare scene transition; rimuovere callback host; cancellare scope Logic; cancellare owner Script; disconnettere gateway da lifecycle; svuotare queue; impedire callback verso oggetti già distrutti.

**Restart test obbligatori**: `create → load → play → stop` ripetuti nella stessa applicazione.

**Gate RU-02f**: `Application` non possiede moduli gameplay; nessun puntatore pendente; shutdown idempotente; restart ripetibile; nessuna callback verso `Application` distrutta; Leak/ASan pass, dove disponibile.

### RU-02g — Confine frame e presentazione

**Obiettivo**: impedire che il renderer interroghi direttamente oggetti runtime mutabili durante il draw.

**Percorso**: riutilizzare il sistema esistente di `SceneFrameSnapshot` e i relativi builder, senza introdurre un secondo modello render.

```
GameplaySession
→ SceneFrameBuilder condiviso
→ GameplayFrameSnapshot immutabile
→ Application::renderActiveScene
→ Renderer
```

API: `GameplayFrameSnapshot GameplaySession::buildFrameSnapshot() const;`

Lo snapshot contiene esclusivamente dati renderizzabili: transform; sprite; source rect; Tilemap render cells; visibility; layer/order; camera; text/UI futura. Non contiene: puntatori mutabili al World; riferimenti al `ProjectDocument`; callback; handle authoring; file paths non risolti per frame.

**Gate RU-02g**: rendering legge soltanto snapshot; nessun draw nella sessione; nessuna query authoring durante render; snapshot stabile per l'intero frame; test golden/parity invariati.

### RU-02h — Pulizia e congelamento API

**Obiettivo**: eliminare tutti i percorsi transitori.

**Rimuovere**: `Application::tickFixedStep`; `Application::dispatchGameplayCollisionTransitions`; ownership World/Logic/Script da `Application::Modules`; accessi pubblici ai moduli interni; adapter temporanei verso concrete class non più necessari; duplicazioni di `EngineContext`; wiring gameplay dentro `EditorAPI`.

**Dependency gates CMake**: `GameplaySession` non deve linkare direttamente RmlUi; `editor-native`; `EditorAPI`; renderer di piattaforma (salvo una piccola interfaccia presentation separata); window management.

Test o regola CI (grep/include dependency guard): nessun include `editor-api.h`; nessun include RmlUi; nessuna chiamata `BeginDrawing`/`EndDrawing`.

**Gate RU-02h**: API pubblica congelata; solo porte esplicite; nessun percorso legacy; documentazione aggiornata; native/WASM/headless verdi.

## 7. RU-03 — Integrazione con Editor Play RmlUi

RU-02 termina quando il runtime reale utilizza `GameplaySession`. RU-03 sostituisce l'attuale simulazione editoriale.

**Percorso**: `ProjectDocument → validazione executable → RuntimeProjectSnapshot → GameplaySession`.

L'editor fornisce: host audio; input snapshot; asset resolver confinato; frame viewport; diagnostiche; Stop lifecycle.

**Eliminazione del vecchio path**: `EditorNative::PlaySession` può sopravvivere soltanto temporaneamente come façade:

```cpp
class EditorPlaySession {
    std::unique_ptr<GameplaySession> runtime_;
};
```

Non può mantenere: fisica propria; movimento proprio; collision system proprio; host Logic proprio; runtime Script proprio; animazione propria; spawn stub.

Alla fine la façade può essere rimossa o restare come semplice controller editoriale.

## 8. Gestione del EngineContext

**Fase transitoria**: conservare un solo `EngineContext` per ridurre il rischio. La sessione riceve una view dello stesso context, senza copiarlo.

**Fase finale**: separare:

```cpp
struct GameplayContext {
    World* world;
    Physics* physics;
    RuntimeEntityGateway* entities;
    VariableManager* variables;
    EventBus* events;
    CameraManager* camera;
    SpriteAnimator* animator;
};

struct GameplayHostPorts {
    IGameplayAudioService* audio;
    IGameplayDialogGate* dialog;
    IRuntimeAssetResolver* assets;
    IRuntimePresentationCommands* presentation;
    IRuntimeStorageService* storage;
};
```

`GameAPI` deve ricevere soltanto ciò che le sue binding richiedono. Non deve continuare indefinitamente ad avere accesso a ogni servizio dell'applicazione.

## 9. Ordine di merge consigliato

Ogni commit deve compilare e mantenere i test verdi.

1. `test(runtime): characterize gameplay tick order`
2. `docs(runtime): classify host and gameplay ownership`
3. `refactor(app): move frame preparation outside fixed tick`
4. `refactor(runtime): add non-owning GameplaySession`
5. `refactor(runtime): move fixed tick into GameplaySession`
6. `refactor(runtime): introduce immutable GameplayInputFrame`
7. `refactor(runtime): delegate Application input dispatch`
8. `refactor(runtime): move gameplay composition from Application`
9. `refactor(runtime): transfer gameplay module ownership`
10. `refactor(runtime): move shutdown into GameplaySession`
11. `refactor(runtime): expose immutable frame snapshot`
12. `refactor(app): render from GameplaySession snapshot`
13. `cleanup(runtime): remove legacy Application gameplay path`
14. `test(runtime): native/WASM/headless parity gate`

Evitare un singolo commit che sposta ownership, modifica tick, cambia input, modifica rendering e modifica `EngineContext` contemporaneamente.

## 10. Test gate complessivi

**Composizione**: una sola istanza World; una sola istanza gateway; una sola istanza Logic Runtime; una sola istanza Script Runtime; una sola registrazione GameAPI; una sola VM Lua runtime.

**Tick**: ordine esatto; dialog blocking; Physics Auto/On/Off; Script prima del Platformer; lifecycle prima e dopo auto-destroy; animation events una volta; collision pairs deterministici.

**Input**: stesso snapshot a Logic e Script; input sintetico headless; nessun polling platform nella sessione; edge reset host-side.

**Scene lifecycle**: start scene; restart; transition; restore; scope install; scope cancel; spawn runtime; destroy runtime.

**Restart e lifetime**: create/destroy session ripetuto; load project A → stop → project B; callback disconnesse; nessun uso dopo free; shutdown parziale dopo init fallito.

**Parità**: con stesso progetto, input e sequenza dt: `game.exe = WASM = GameplaySession headless = Editor Play dopo RU-03`. Confrontare almeno: entity IDs; transforms; visibility; active scene; variables; animation frame; collision events; spawn/destroy order; diagnostiche.

## 11. Rischi principali e mitigazioni

| Rischio | Mitigazione |
|---|---|
| Callback che catturano `Application` | Trasferirle insieme all'ownership |
| Ordine init modificato | Characterization e boot-step test |
| Ordine shutdown errato | Reverse dependency map |
| Due `EngineContext` | Un solo context transitorio |
| Renderer ancora usato da World | Port presentation ristretto, migrazione progressiva |
| Dialog gameplay/presentation misto | `IGameplayDialogGate` |
| Input GameAPI diverso da Logic/Script | Un solo `GameplayInputFrame` |
| WASM bridge dentro la sessione | Bridge resta host e inoltra richieste |
| Frame snapshot diverso tra host | Un solo builder condiviso |
| Sessione headless che diverge | Stessa composizione, servizi host sostituibili |
| Init fallito a metà | Teardown transazionale per fasi |
| Vecchio PlaySession ancora attivo | RU-03 elimina ogni algoritmo duplicato |

## 12. Stop-the-line

Il lavoro deve fermarsi quando compare uno di questi casi:

- `Application` e `GameplaySession` costruiscono entrambe `World`.
- Esistono due fixed tick.
- Logic viene installata da due percorsi.
- Script viene installato da due percorsi.
- Esistono due host runtime differenti.
- `GameplaySession` include `editor-api.h`.
- `GameplaySession` chiama il renderer per disegnare.
- `GameplaySession` polla Raylib Input.
- Il renderer modifica il runtime durante il draw.
- Il runtime legge il `ProjectDocument` durante Play.
- Editor Play mantiene collisioni o movimento propri.
- Native e WASM usano composition root differenti.
- La sessione possiede una copia sincronizzata di dati authoring.
- Una callback host modifica direttamente moduli interni alla sessione.
- L'ordine Logic/Script/Physics cambia accidentalmente.
- Un test headless passa mentre `game.exe` continua a usare un altro percorso.
- `EngineContext` viene duplicato per "comodità".
- La sessione riceve direttamente l'intera `Application`.
- Un servizio opzionale produce branching comportamentale silenzioso.
- Il vecchio codice viene lasciato "temporaneamente" senza una fase esplicita di rimozione.

## 13. Definition of Done di RU-02

RU-02 è conclusa quando:

- `Application` è un host, non un simulatore;
- `GameplaySession` contiene l'unica implementazione gameplay;
- esiste un solo fixed tick;
- `game.exe` usa `GameplaySession`;
- WASM usa `GameplaySession`;
- input platform viene convertito in snapshot immutabile;
- Logic e Script usano lo stesso runtime host;
- World, gateway e scene lifecycle sono posseduti dalla sessione;
- spawn funziona nel runtime condiviso;
- collision enter/exit passa dallo stesso percorso;
- GameAPI e LuaHost vengono registrati una sola volta;
- EditorAPI resta fuori dalla sessione;
- nessun rendering avviene nella sessione;
- il renderer consuma uno snapshot immutabile;
- nessun accesso authoring avviene durante il tick;
- shutdown e restart sono affidabili;
- non esistono due context divergenti;
- native, WASM e headless superano gli stessi test;
- l'API pubblica della sessione non espone i suoi moduli interni.

L'iniziativa complessiva sarà conclusa dopo RU-03, quando anche l'editor RmlUi utilizzerà la stessa sessione e l'attuale `EditorNative::PlaySession` non conterrà più alcuna simulazione indipendente.

**Clausola finale congelata**: `GameplaySession` è l'unica autorità della simulazione runtime. Viene estratta dal codice reale di `Application`, non ricostruita in parallelo. `Application`, l'editor RmlUi e il Web host forniscono esclusivamente servizi di piattaforma, input e presentazione. Tutti gli host materializzano e pilotano la stessa sessione; nessun host implementa gameplay, nessun dato authoring viene sincronizzato durante Play e nessun percorso alternativo viene mantenuto.

---

# Debt register RU-02/RU-03

Prima di procedere conviene formalizzare un debt register di RU-02/RU-03, distinguendo: debito che deve essere necessariamente eliminato; debito transitorio ammesso durante il refactor; debito reale ma rinviabile; elementi che sembrano duplicazioni, ma sono invece proiezioni legittime.

**Regola generale**: è debito da cancellare qualsiasi elemento che conservi una seconda autorità, un secondo ciclo di vita o un secondo percorso comportamentale per la stessa responsabilità.

## 14. Debito P0 da eliminare obbligatoriamente

### D-01 — `EditorNative::PlaySession` come secondo runtime

Il debito più importante. L'attuale editor Play possiede strutture e algoritmi propri per: entità runtime; TopDown e Platformer; collider AABB; movimento kinematico; Tilemap runtime; Sprite Animator; transizioni di collisione; Logic host; Script Runtime; variabili; audio command queue. La classe contiene proprie strutture `RuntimeEntity`, `RuntimePlatformerController`, `RuntimeBoxCollider`, `RuntimeScene` e propri metodi `moveKinematicEntity()`, `dispatchCollisionTransitions()` e `flushPendingDestroys()`.

Questo non è soltanto codice duplicato. È una seconda definizione della semantica gameplay: runtime reale = `World + Gateway + Physics`; Editor Play = `RuntimeEntity + AABB + movimento custom`.

**Destinazione**: alla fine di RU-03, `EditorNative::PlaySession` eliminata oppure ridotta a una façade editoriale senza gameplay (vedi §7). La façade può gestire: Start/Stop; asset preload editoriale; routing input; raccolta diagnostiche; aggiornamento UI. Non può contenere simulazione.

**Gate di cancellazione**: nessun `RuntimeEntity` editoriale parallelo; nessuna fisica editoriale; nessun host Logic editoriale; nessun Script Runtime editoriale; nessuna animazione editoriale runtime; nessun collision tracking duplicato.

### D-02 — Fixed tick dentro Application

`Application::tickFixedStep()` è attualmente l'autorità della simulazione, ma `Application` dovrebbe essere l'host. Il metodo contiene l'intera sequenza gameplay: sistemi temporali; World; animazioni; Logic; Lua; Script; controller; Physics; lifecycle; collisioni; audio.

**Destinazione**: alla fine di RU-02 deve esistere soltanto `GameplaySession::tickFixedStep(float dt);`. `Application` può conservare soltanto il loop (vedi §3.2). Non deve restare un wrapper chiamato `Application::tickFixedStep()` senza una ragione concreta — anche un wrapper triviale può diventare il punto in cui, in futuro, viene reinserita logica host o gameplay.

### D-03 — Doppia gestione delle collisioni

Il runtime reale mantiene `Application::dispatchGameplayCollisionTransitions()` con il proprio set `activeGameplayCollisionPairs`, e l'Editor Play possiede contemporaneamente `PlaySession::dispatchCollisionTransitions()` con il proprio stato. Il runtime reale costruisce le transizioni dalla collision world e le invia prima a Logic e poi agli Script; l'Editor Play implementa la propria rilevazione confrontando AABB e coppie attive.

**Destinazione**: una sola implementazione interna a `GameplaySession`: `CollisionWorld events → immutable collision transition snapshot → Logic Runtime → Script Runtime`. Il tracking delle coppie deve appartenere alla sessione, non all'host.

### D-04 — Composition root gameplay dentro Application::initSubsystems()

`Application::initSubsystems()` costruisce e collega nello stesso blocco: Renderer; Physics; Input; Audio; gateway; scene lifecycle; World; Logic host; Logic Runtime; GameAPI; LuaHost; Dialog; EditorAPI bridge. Questo rende `Application` contemporaneamente: platform host; composition root; simulatore; bridge editoriale; gestore di scene; gestore Lua.

**Destinazione**: il graph gameplay deve essere costruito una sola volta da `GameplaySession::initialize(...)`. `Application` deve costruire esclusivamente servizi host e passare porte esplicite.

### D-05 — Application::Modules come contenitore misto

La struttura possiede nello stesso livello Renderer, Input, EditorViewport, Physics, World, Gateway, Logic, Script, GameAPI, LuaHost, Camera, Audio, Dialog. Non è soltanto una struttura grande: rende ambiguo chi possiede cosa e permette a qualsiasi unità di implementazione di accedere a ogni modulo.

**Destinazione**: `Application::HostModules` (platform/device/presentation) + `GameplaySession::Modules` (simulation/gameplay), ownership non sovrapposta. Nessun puntatore allo stesso modulo deve essere posseduto da entrambi.

### D-06 — RuntimeLogicHostAdapter collocato nell'application layer

L'adapter è già il vero host runtime e supporta transform, movement, Physics, animation, audio, state, spawn reale, destroy. È però definito in `app_modules.h`, quindi appare come implementazione privata di `Application`.

**Destinazione**: spostarlo nel modulo gameplay (`runtime/gameplay/runtime_logic_host_adapter.*`) oppure renderlo dettaglio interno di `GameplaySession`. Non deve esistere un altro adapter equivalente nell'editor.

### D-07 — Input dispatch distribuito nell'host

`Application::loopIteration()` oggi: polla il dispositivo; traduce i tasti; invia direttamente a Logic; costruisce lo snapshot Script; esegue flush; invia eventi GameAPI. L'host conosce quindi troppo dell'ordine gameplay.

**Destinazione**: `Application` produce solo `GameplayInputFrame` (poll platform); `GameplaySession` fa Logic → Script → flush → GameAPI. L'unica responsabilità dell'host è produrre uno snapshot.

### D-08 — Callback gameplay che catturano Application

Il wiring usa callback `[this]` per: installazione degli scope; gestione scene; invalidazioni; reset; restore. Finché l'ownership è in `Application`, è comprensibile; dopo l'estrazione diventerebbe un'inversione sbagliata (`GameplaySession`-owned World → callback verso `Application` → `Application` modifica `GameplaySession` internals).

**Destinazione**: le callback che operano sul gameplay devono catturare `GameplaySession`. Le notifiche verso l'host devono essere eventi outward-only: `onSceneTransition(result)`, `onRuntimeDiagnostic(diagnostic)`, `onPresentationInvalidated(flags)`. L'host non deve richiamare direttamente metodi interni del World o del gateway.

### D-09 — Ownership del collision state dentro Application::Modules

`activeGameplayCollisionPairs` è stato di simulazione ma vive nel container dell'`Application`.

**Destinazione**: deve diventare membro privato di `GameplaySession`. Regola generale: dato necessario tra due fixed tick → session-owned, non host-owned.

### D-10 — Rendering preparato nel fixed tick

La chiamata `renderer->clearDrawQueue();` è dentro `tickFixedStep()`. È piccola, ma viola il confine e può diventare il precedente per aggiungere altro rendering nella sessione.

**Destinazione**: spostarla nel frame host (`begin host frame → clear draw queue → zero o più fixed tick → build snapshot → render`). Deve essere eliminata dal percorso simulation.

## 15. Debito P0/P1 nel grafo delle dipendenze

### D-11 — EngineContext troppo ampio

`EngineContext` espone nello stesso contenitore Renderer, Physics, Input, Audio, LuaHost, SceneManager, gateway, AssetLoader, GameAPI, World, profiler, camera, animator, SaveLoad, Dialog. Il commento dice inoltre che il lifetime è gestito da `Application`, presupposto che diventerà falso dopo il trasferimento dell'ownership.

Non va eliminato immediatamente in RU-02a, perché riscrivere insieme composition root e dependency injection aumenterebbe il rischio.

**Debito transitorio accettabile**: durante le prime sotto-fasi, un solo `EngineContext` esistente → `GameplaySession` lo usa come view.

**Destinazione**: separazione in `GameplayContext` / `GameplayHostPorts`, oppure dipendenze costruttore più strette per GameAPI. Il debito è considerato cancellato quando la simulazione non vede EditorViewport, non può controllare la finestra, non possiede accesso generale al renderer, i moduli non "pescano" arbitrariamente servizi non dichiarati.

### D-12 — World::setRenderer() e dipendenza concreta dalla presentazione

L'attuale bootstrap collega direttamente `world->setRenderer(renderer);` — rende meno netto il confine World → Renderer concreto.

**Valutazione necessaria**: verificare quali metodi del renderer vengono realmente usati dal World (query camera? creazione draw command? shader? testo? debug? dimensioni viewport?).

**Destinazione**: `World → produce dati → SceneFrameBuilder → Renderer`, oppure per comandi presentation indispensabili `IRuntimePresentationCommands` con API stretta. Non passare il renderer completo alla sessione come soluzione definitiva.

### D-13 — GameAPI dipendente da un service locator generale

`GameAPI` viene costruita con l'intero `EngineContext` e registra molte aree diverse, tra cui camera, shader, dialog, save e componenti — difficile sapere quali binding richiedano veramente servizi host.

**Destinazione**: procedere gradualmente verso `GameAPI(GameplayServices gameplay, GameplayHostPorts host);` oppure binder per dominio con dipendenze esplicite (`bindAudioAPI`, `bindCameraAPI`, `bindSaveAPI`). Non serve completarlo prima del primo estratto funzionante, ma deve essere registrato come debito.

### D-14 — Audio con proprietà e responsabilità ambigue

L'Audio è: device/platform service; consumato dal gameplay; aggiornato nel fixed tick; usato direttamente dal Logic host.

**Valutazione**: separare concettualmente Audio backend/device (host-owned) da Audio gameplay commands/state (session-owned o port). Per RU-02 può restare host-owned e passato tramite `IGameplayAudioService`. Non duplicare cache o dispositivi dentro la sessione.

### D-15 — DialogManager come ibrido UI/gameplay

Il `DialogManager` viene costruito nell'host, riceve `EngineContext`, blocca il gameplay, viene aggiornato nel fixed tick.

**Destinazione**: separare Dialog gameplay state/gate (session-facing interface, `IGameplayDialogGate`) da Dialog rendering/input surface (host). Non lasciare `GameplaySession` dipendente dalla classe UI concreta.

## 16. Debito di lifecycle e affidabilità

### D-16 — Inizializzazione monolitica con bool

`initModules()` concatena `initUtilities() && initSubsystems() && loadProject()`. Ogni funzione costruisce numerosi moduli in sequenza e ritorna semplicemente `false` al primo errore.

**Rischi**: teardown parziale; messaggi poco strutturati; impossibilità di sapere quali moduli siano inizializzati; callback installate prima del fallimento; differenze tra error path native/WASM.

**Destinazione**: un risultato strutturato (`GameplaySessionCreateError{stage, message}`) e teardown RAII dello stato parzialmente costruito. Il debito è cancellato quando un errore a qualsiasi boot stage non perde risorse, non lascia wiring statico, non lascia callback, può essere riportato all'editor.

### D-17 — Shutdown manuale troppo lungo e fragile

`shutdownModules()` conosce manualmente l'ordine di distruzione di ogni modulo. L'ordine è necessario, ma oggi è distribuito nello stesso monolite che possiede host e gameplay.

**Destinazione**: `GameplaySession::shutdown()` + `Application::shutdownHost()`, con callback disconnesse prima della distruzione. Serve anche un test: init fallito a metà → distruzione sicura.

### D-18 — Static wiring di EditorAPI

Il bootstrap usa numerose funzioni statiche (`EditorAPI::wireEngine`, `wireLua`, `wireRenderer`, ...). Questo è debito host, non gameplay.

**Regola**: non è necessario rifattorizzarlo interamente in RU-02, ma non deve entrare in `GameplaySession`; deve essere resettato sempre; non deve essere richiesto dal runtime headless; nessuna callback gameplay deve dipendere dalla presenza di `EditorAPI`. Il futuro miglioramento sarebbe un bridge istanziabile, ma non deve ampliare lo scope di RU-02.

### D-19 — Stato WASM dentro Application

`Application` contiene `webInstance_`, callback statiche Emscripten, branch `ARTCADE_WASM`, bridge editoriale — legittimo nell'host Web.

Diventa debito solo se una di queste condizioni entra nella sessione: `#ifdef ARTCADE_WASM` dentro `GameplaySession`; `EditorAPI::s_mode` dentro `GameplaySession`; emscripten callback dentro `GameplaySession`.

La sessione deve ricevere semplicemente: `simulating = true/false`, input frame, fixed dt.

## 17. Debito API da cancellare

### D-20 — Accesso mutabile agli interni della sessione

L'attuale Editor Play espone `RuntimeScene& scene();`, `std::vector<RuntimeEntity>& entities();`, `const ScriptRuntime* scriptRuntime();`. Queste API permettono al chiamante di mutare il runtime fuori dal tick, saltare gateway e lifecycle, introdurre percorsi alternativi, dipendere dai container interni.

**Destinazione**: la futura `GameplaySession` deve esporre `buildFrameSnapshot()`, `inspectEntity(EntityId) const`, `drainDiagnostics()`. Nessun container mutabile.

### D-21 — Diagnostiche scritte direttamente su std::cerr

Il fixed tick stampa direttamente gli errori Script — lega la simulazione a una policy di output.

**Destinazione**: `std::vector<RuntimeDiagnostic> drainDiagnostics();`. L'host decide se stampare su console, inviare al Problems panel, scrivere un log, mostrare un dialog, raccogliere telemetria.

### D-22 — Profiling concreto dentro la simulazione

`tickFixedStep()` aggiorna direttamente `profiler_`.

**Destinazione**: una porta `IRuntimeProfilerSink` oppure metriche restituite dalla sessione. La sessione non deve dipendere dal profiler posseduto da `Application`.

## 18. Debito transitorio ammesso

Alcuni elementi possono sopravvivere temporaneamente, ma devono avere una fase di rimozione dichiarata.

- **T-01 — `GameplayRuntimeRefs`**: legittimo in RU-02c per spostare il tick prima dell'ownership. Diventa debito appena RU-02e trasferisce il composition root. **Scadenza**: eliminare in RU-02f.
- **T-02 — Renderer concreto passato alla sessione**: può essere mantenuto temporaneamente per far funzionare GameAPI o World. **Scadenza**: sostituire con `IRuntimePresentationCommands` entro RU-02h o una sotto-slice immediatamente successiva. Non deve diventare API pubblica stabile.
- **T-03 — Unico EngineContext condiviso**: accettabile durante la migrazione per evitare due context divergenti. **Scadenza**: dopo il trasferimento dell'ownership, `EngineContext` monolitico → `GameplayContext` + host ports.
- **T-04 — Wrapper `Application::tickFixedStep()`**: può esistere per un commit (`void Application::tickFixedStep(float dt) { gameplaySession_->tickFixedStep(dt); }`). **Scadenza**: eliminarlo quando tutti i call site sono migrati.
- **T-05 — Vecchio Editor Play mantenuto durante RU-02**: RU-02 modifica il runtime reale; l'editor può continuare temporaneamente a usare la vecchia `PlaySession`. **Scadenza**: RU-03 deve eliminarne ogni algoritmo. Non è accettabile dichiarare conclusa l'iniziativa con entrambi i runtime ancora presenti.

## 19. Cosa non deve essere considerato debito

Non tutto ciò che copia dati è una seconda autorità.

- **Runtime snapshot** (`ProjectDocument → RuntimeProjectSnapshot`): è una materializzazione legittima, purché sia immutabile, venga creata una volta, non venga sincronizzata continuamente, non venga scritta indietro.
- **Frame snapshot** (`GameplaySession → GameplayFrameSnapshot`): è una proiezione per il renderer, non un secondo runtime.
- **Texture e audio cache**: sono risorse derivate host-side, non autorità authoring.
- **Input snapshot**: è una fotografia immutabile del dispositivo per quel frame, non una copia autorevole dell'input.
- **Adapter host**: legittimo quando non contiene logica gameplay, traduce una porta, non mantiene stato duplicato, non decide semantica. Esempio corretto: `RaylibAudioAdapter → implementa IGameplayAudioService`.

## 20. Debt register — tabella riepilogativa

| ID | Debito | Priorità | Eliminazione |
|---|---|---|---|
| D-01 | Editor Play runtime parallelo | P0 | RU-03 |
| D-02 | Fixed tick in Application | P0 | RU-02c/h |
| D-03 | Collision dispatch duplicato | P0 | RU-02c + RU-03 |
| D-04 | Composition root gameplay in Application | P0 | RU-02e |
| D-05 | Application::Modules misto | P0 | RU-02f |
| D-06 | Logic host nell'application layer | P0 | RU-02e |
| D-07 | Input gameplay dispatch nell'host | P0 | RU-02d |
| D-08 | Callback gameplay verso Application | P0 | RU-02e/f |
| D-09 | Collision state host-owned | P0 | RU-02c |
| D-10 | Renderer preparation nel fixed tick | P0 | RU-02b |
| D-11 | EngineContext monolitico | P1 | RU-02h/post |
| D-12 | World dipendente dal Renderer | P1 | RU-02g/h |
| D-13 | GameAPI service locator | P1 | post RU-02 |
| D-14 | Audio ownership ambiguo | P1 | RU-02e/f |
| D-15 | Dialog ownership ambiguo | P1 | RU-02e/f |
| D-16 | Init monolitica e errori bool | P1 | RU-02e/f |
| D-17 | Shutdown manuale misto | P0 | RU-02f |
| D-18 | EditorAPI static wiring | P2 host | post RU-03 |
| D-20 | Accesso mutabile agli interni | P0 | RU-02h |
| D-21 | Diagnostiche su stderr | P1 | RU-02c/h |
| D-22 | Profiler concreto nella sessione | P1 | RU-02c/e |

## 21. Criterio per considerare il debito cancellato

Non basta spostare i file. Il debito è cancellato soltanto quando: vecchio path → non compilato → non linkato → non richiamabile → non mantenuto come fallback.

Per ogni voce P0 devono esistere controlli concreti: grep/include gate; CMake dependency gate; test di singola istanza; test di parità; eliminazione dei tipi legacy; eliminazione dei metodi legacy; nessun branch "old runtime".

Esempi:

```
grep EditorNative::RuntimeEntity
→ nessun risultato dopo RU-03

grep Application::dispatchGameplayCollisionTransitions
→ nessun risultato dopo RU-02

GameplaySession target
→ non linka editor-api
→ non linka RmlUi

game.exe / WASM / Editor Play
→ costruiscono la stessa GameplaySession
```

**Debt cancellation rule**: ogni struttura, adapter o percorso transitorio introdotto durante RU-02 deve dichiarare la fase esatta in cui verrà eliminato. Il refactor non è concluso quando il nuovo percorso funziona, ma quando il vecchio percorso non è più compilato né raggiungibile. Le copie immutabili necessarie tra cicli di vita sono ammesse; i mirror mutabili, gli algoritmi paralleli e i composition root equivalenti sono debito bloccante.

La priorità assoluta è cancellare: Editor PlaySession parallela; Application gameplay tick; composition root misto; collision/input dispatch duplicati; accesso mutabile agli interni. Gli altri miglioramenti, come restringere `EngineContext` e rendere `EditorAPI` istanziabile, sono debiti reali ma non devono trasformare RU-02 in una riscrittura totale del runtime.
