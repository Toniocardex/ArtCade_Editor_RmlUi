# ArtCade RmlUi — Play/Export Runtime Unification Roadmap

**Stato:** pianificato
**Ambito:** editor desktop nativo (`ArtCade_Editor_RmlUi`) e runtime condiviso (`ArtCade-Studio_V2/runtime-cpp`, collegato tramite junction `vendor/artcade-runtime`)
**Scopo:** eliminare la doppia simulazione Editor Play / gioco esportato (blocco P0-A dell'audit tecnico) e riusare lo stesso lavoro come fondazione per l'Export (blocco P0-B)
**Baseline:** audit tecnico + investigazione architetturale del 2026-07-20 (vedi §2); studio di dettaglio RU-01/RU-02/RU-03 del 2026-07-20 (vedi rispettive sezioni)
**Ultimo aggiornamento:** 2026-07-20

## 1. Autorità e precedenza

Questa roadmap è subordinata, nell'ordine, a:

1. `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`;
2. `ARTCADE_RMLUI_ARCHITECTURE.md`;
3. `ARTCADE_RMLUI_ENGINEERING_GATES.md`;
4. `RMLUI_MIGRATION_CONTRACT.md`;
5. il vincolo di Edit/Play isolation già stabilito (Play non deve leggere/scrivere la camera workspace di `EditorState` — vincolo esistente, non negoziabile durante la migrazione).

In caso di conflitto prevale il documento di livello superiore. Nessuna slice di questa roadmap autorizza doppie autorità sui dati, sincronizzazioni temporanee, feature flag permanenti o dipendenze del runtime esportato verso codice editor-only.

**Ambito cross-repo esplicito**: le slice RU-02, RU-03 e RU-04 toccano `ArtCade-Studio_V2/runtime-cpp`, non solo questo repository. Ogni slice che tocca il runtime deve lasciarlo compilabile e testabile in isolamento (il gioco esportato standalone non deve mai acquisire una dipendenza verso `artcade-editor-core`/`artcade-editor-native`).

## 2. Diagnosi verificata (riferimento)

Fatti confermati nel codice, non solo ipotesi dell'audit iniziale:

- Il repo editor collega l'intero albero CMake del runtime via junction (`CMakeLists.txt:17-29`: `vendor/artcade-runtime` → `ArtCade-Studio_V2/runtime-cpp`, poi `add_subdirectory`), ma `artcade-editor-native`/`artcade-editor-core` (`src/CMakeLists.txt:84-92,212-218`) non collegano i target di simulazione reali: `World`, `runtime-entity-gateway`, `scene-system`, `physics`, `camera-manager`, `game-api`, `renderer`.
- `PlaySession` (`src/editor-native/model/play_session.h/.cpp`) reimplementa a mano: movimento cinematico + risoluzione AABB (`play_session.cpp:299-327`, `1032-1079`), controller top-down/platformer (`:1127-1167`), transizioni di collisione enter/exit (`:845-895`) — mentre il runtime reale usa `World::tick*` (`world.h:69-203`) e drena `World::collisionEvents()`.
- `PlaySession` usa un proprio ECS ad hoc, `RuntimeEntity` (`play_session.h:85-104`, 6 componenti opzionali in un `std::vector` flat), mentre il runtime reale usa `RuntimeEntityGateway`, un registro EnTT con 15+ tipi di componente, pooling e spawn-from-class (`runtime-entity-gateway.h:41-334`).
- `PlaySession::LogicHostAdapter::spawnObjectType` (`play_session.cpp:149-153`) è uno stub che ritorna sempre `INVALID_ENTITY` ("Editor Play spawn is not implemented in this slice"). L'equivalente reale, `RuntimeLogicHostAdapter::spawnObjectType` (`app_modules.h:168-181`), è completo. Entrambi implementano la stessa interfaccia già condivisa `IGameplayRuntimeHost`/`ILogicRuntimeHost` (`gameplay-runtime-host.h:13-69`, `logic-runtime.h:29`) — è la seam esistente su cui costruire la migrazione.
- `ProjectDocument` legge/scrive tramite un parser JSON proprio di ~2200 righe (`src/editor-native/model/project_io.cpp`), indipendente dal parser canonico del runtime (`ProjectJson::*` / `AssetLoader::parseProjectJson`, target `artcade-project-json`) usato dal gioco esportato. Seconda superficie di duplicazione, indipendente dalla simulazione.
- Moduli già effettivamente condivisi, nessuna duplicazione: Logic Board (`artcade-logic-core`, `artcade-logic-runtime`), Script/Lua (`artcade-script-runtime`), avanzamento animazioni (`artcade-sprite-animation-core`), tipi di dominio (`artcade-core`, incl. `ProjectDoc`).

## 3. Obiettivo finale

La migrazione è conclusa soltanto quando:

- Editor Play e gioco esportato (Windows e Web) eseguono la stessa simulazione (World/gateway/physics/Logic/Script/camera), non due implementazioni parallele;
- `PlaySession`, `RuntimeEntity`, `RuntimeScene` e la logica di movimento/collisione duplicata sono rimossi dal repo editor;
- `ProjectDocument` legge tramite lo stesso parser canonico (`ProjectJson::*`) usato dal runtime esportato;
- i 3 "gate games" (platformer, top-down, puzzle/arcade — vedi audit originale §20) si comportano in modo identico tra Editor Play e build esportata;
- `runtime-cpp` resta compilabile e testabile in isolamento, senza dipendenze verso codice editor-only;
- nessuna regressione nei test esistenti, editor (`editor-core-test`, `tileset-tilemap-test`, ecc.) e runtime;
- il vincolo di Edit/Play camera isolation non regredisce (verificabile con gli stessi `--shot-play`/`--shot-pan`/`--shot-zoom` già esistenti).

## 4. Regole di esecuzione

- Una slice risolve un solo problema architetturale principale.
- Ogni slice lascia sia l'editor sia il runtime (quando toccato) compilabili e testabili.
- Nessuna slice inizia prima del gate di uscita di quella da cui dipende (vedi tabella §5).
- Nessun test viene rimosso, indebolito o disabilitato per far passare una slice.
- La camera Play isolata già costruita (`computeFitZoom`, `scene_view_camera.h/.cpp`) non viene ributtata: in RU-04 diventa il profilo di default quando la scena non ha una `Camera2D` autorata esplicitamente, componendosi col vero `CameraManager` invece di essere sostituita.
- Nessun manager/event-bus/DI-framework nuovo: le astrazioni introdotte (es. `GameplaySession`, `EditorGameplayRuntimeHost`) devono mappare 1:1 su un confine architetturale reale già identificato in §2, non essere generiche.
- Verifica per ogni slice: `scripts\build.bat --test` verde (editor) + build/test del runtime se toccato + screenshot headless (`--shot-*`) pertinenti — stesso standard già in uso nel resto del progetto.
- Ogni commit deve avere rollback comprensibile; nessuna slice lascia uno stato "a metà" (niente feature flag permanenti).

## 5. Stato sintetico

| ID | Priorità | Slice | Stato | Dipendenze | Repo toccati |
|---|---|---|---|---|---|
| RU-00 | Preparazione | Congelamento baseline e branch di migrazione | [x] | — | editor |
| RU-01a | P0 | Riconciliazione schema canonico di progetto (formatVersion, scenes, componenti mancanti) | [x] | RU-00 | runtime, editor |
| RU-01 | P0 | Parser `ProjectDocument` unificato sul canonico `ProjectJson::*` | [ ] | RU-01a | editor |
| RU-02a | P0 | Characterization e dependency map del composition root/tick attuali | [x] | RU-00 | runtime |
| RU-02b | P0 | Separare preparazione host (`clearDrawQueue`) dal fixed tick | [x] | RU-02a | runtime |
| RU-02c | P0 | `GameplaySession` non-owning: sposta l'algoritmo del tick (reference, non ownership) | [x] | RU-02b | runtime |
| RU-02d | P0 | Confine input immutabile (`GameplayInputFrame`, niente polling nella sessione) | [x] | RU-02c | runtime |
| RU-02e | P0 | Estrarre il composition root gameplay in `GameplaySession::initialize()` | [x] | RU-02d | runtime |
| RU-02f | P0 | Trasferimento ownership dei moduli gameplay + shutdown/restart affidabili | [x] | RU-02e | runtime |
| RU-02g | P0 | Confine frame/presentazione: renderer legge solo `GameplayFrameSnapshot` | [x] | RU-02f | runtime |
| RU-02h | P0 | Pulizia e congelamento API pubblica di `GameplaySession` | [x] | RU-02g | runtime |
| RU-03 | P0 | Integrazione Editor Play con `GameplaySession`; `EditorNative::PlaySession` → façade o rimossa | [ ] | RU-02h, RU-01, RU-01a | editor, runtime |
| RU-04 | P0 | Play-start come "export a temp + load via `AssetLoader`" | [ ] | RU-01, RU-01a, RU-03 | editor |
| RU-05 | P0 | Ritiro di `PlaySession`/`RuntimeEntity`/`RuntimeScene` e codice duplicato | [ ] | RU-04 | editor |
| GATE-FINAL | Gate | Parity audit sui 3 gate games, Windows + Web | [ ] | RU-05 | editor, runtime |

Stati ammessi: `[ ]` non iniziato, `[-]` in corso, `[x]` completato, `[!]` bloccato con motivazione registrata.

**RU-02 e RU-03 hanno un piano di dettaglio congelato separato**: [RU02_GAMEPLAY_SESSION_REFACTOR.md](RU02_GAMEPLAY_SESSION_REFACTOR.md) — decisione architetturale (`GameplaySession` estratta da `Application`, non ricostruita in parallelo), 10 clausole progettuali non negoziabili, confine host/gameplay dei moduli, API pubblica congelata, le 8 sotto-fasi RU-02a→RU-02h con gate di uscita propri, l'integrazione RU-03 con Editor Play, ordine di merge consigliato, condizioni di stop-the-line, e un debt register completo (D-01…D-22, T-01…T-05) con criterio esplicito di cancellazione del debito. Le sezioni §9-10 di questo documento restano come sintesi/diagnosi iniziale; il documento dedicato prevale per il dettaglio implementativo.

## 6. Fase RU-00 — Congelamento baseline

**Problema**

Prima di una migrazione multi-repo va isolata una baseline pulita, coerente con `BASE-01` della remediation roadmap esistente.

**Attività**

- [x] Registrare `git status --short` in entrambi i repo (editor e `runtime-cpp`).
- [x] Creare branch di migrazione dedicato in entrambi i repo (`feature/runtime-unification`).
- [x] Eseguire build Release + suite test in entrambi i repo, registrare il baseline (conteggio test, warning noti).
- [x] Documentare la versione/commit di `runtime-cpp` puntata dalla junction al momento del congelamento.

**Scoperte impreviste e correzioni applicate (fuori scope di migrazione, necessarie per una baseline verde)**

Al primo tentativo di build, `runtime-cpp` main non era autosufficiente: l'editor main dipendeva da modifiche non committate lasciate in una sessione precedente, e la suite di test standalone di `runtime-cpp` aveva bit-rot pre-esistente su due fronti indipendenti. Tutti e tre i problemi sono stati corretti direttamente su `main` di `runtime-cpp` (non sul branch di migrazione), poi il branch è stato ricreato da un `main` pulito:

1. **`7f0a2d97`** — `ScriptApiInsertKind`/`insertKind` (catalogo Script API) erano referenziati da codice già committato lato editor (`script_language_service.cpp:388`, `script_editor_controller.cpp:527,572`, più la suite `script-api-catalog-test`), ma la dichiarazione lato `runtime-cpp` era rimasta solo nel working tree locale, mai committata. Senza questo fix l'editor non compilava affatto.
2. **`4627815e`** — `logic-board-test.cpp` e `runtime-logic-host-parity-test.cpp` costruivano ancora `SpriteRendererComponent`/`SpriteAnimatorComponent`/`SpriteAnimationClipDef` con la shape pre-v9 (mai aggiornati dalla migrazione schema v9, commit `7da13fac`). Impedivano la build standalone di `runtime-cpp` (l'editor non esercita i test di `runtime-cpp`, quindi il problema era invisibile dal lato editor).
3. **`46685757`** — i 4 fixture JSON embedded in `artcade-package-test.cpp` (`loadArtcade`/`loadDirectory`) predatavano sia il gate `formatVersion`/`globalVariables` sia la feature "layer per scena" già presente in `SceneDef` (`project-current-format.cpp:129-240`, `kCurrentProjectFormatVersion = 8`): il loader rifiutava l'intero payload prima di validare qualunque campo applicativo, con 8 assert su 24 falliti.

**Baseline registrata**

- Editor (`ArtCade_Editor_RmlUi`, `scripts\build.bat --test`): build verde — `editor-core-test` 2563/2563, `sprite-animation-test` 233/233, `tileset-tilemap-test` 1476/1476, `generated-sfx-model-test` 480/480, `script-asset-test` 144/144, `script-delete-disk-test` 88/88, `script-text-ops-test` 19/19, `script-api-catalog-test` 195/195, `logic-board-editor-test` 308/308, `generated-sfx-editor-controller-test`/`generated-sfx-generation-service-test`/`play-sound-preload-test` verdi.
- Runtime (`ArtCade-Studio_V2/runtime-cpp`, `build_native.bat`): 48/48 suite CTest, 100% passate.
- `runtime-cpp` `main` @ `46685757` (baseline fissata per la junction `vendor/artcade-runtime`); `feature/runtime-unification` ricreato da questo commit in entrambi i repo, diff vuoto rispetto a `main`.

**Gate di uscita**

- [x] Entrambi i repo compilano e passano i test sulla baseline.
- [x] Nessuna modifica funzionale introdotta da questa fase (le 3 correzioni sono fix di baseline pre-esistenti, non lavoro di migrazione RU-01+).

## 7. Fase RU-01a — Riconciliazione schema canonico di progetto

**Problema (scoperto dallo studio di dettaglio, 2026-07-20 — non previsto dalla stesura iniziale della roadmap)**

RU-01 e RU-04 assumono entrambe che un file scritto dall'editor sia accettato dal loader canonico del runtime (`ProjectJson::validate_current_project_json` → `AssetLoader::parseProjectJson`). Non è vero oggi. Verificato nel codice, non ipotizzato:

- **`formatVersion`**: l'editor scrive `9` (`kCurrentSchemaVersion`, `project_io.cpp:38`); il validator canonico richiede **esattamente** `8` (`kCurrentProjectFormatVersion`, `project-current-format.h:11`) e non ha alcuna logica di migrazione (`project-current-format.cpp:140-145`, nessun simbolo `migrate`/`Migration` in `runtime-cpp/src/core`).
- **`scenes`**: l'editor scrive un **array** (`project_io.cpp:1567-1569,1684`); il validator richiede un **oggetto** chiave-per-id, non vuoto (`project-current-format.cpp:151-154`).
- **`globalVariables`**: l'editor non lo scrive affatto; il validator lo richiede sempre presente (`project-current-format.cpp:155-158`).
- Conseguenza diretta: **un file salvato dall'editor oggi verrebbe rifiutato in blocco dal loader canonico**, prima ancora di validare qualunque campo applicativo. RU-01 ("delega la lettura al parser canonico") e RU-04 ("Play-start = export-a-temp + load via `AssetLoader`") sono entrambe bloccate da questo, non solo RU-01 come inizialmente scoping in tabella.
- **Componenti senza lettore canonico** (verrebbero silenziosamente scartati, non segnalati, se si delegasse la lettura oggi): `EntityDef.boxCollider2D` (`BoxCollider2DComponent`, `project_io.cpp:1169-1205`) — `read_entity_components` (`entity-json.cpp:242-245`) riconosce solo `"physics"`/`"collisionBody"`, non ispeziona affatto la chiave `"boxCollider2D"`. Il componente Tilemap per-istanza a chunk (`project_io.cpp:433-471,629-655`, il modello *corrente* secondo il commento in `types.h:636-639`) non ha lettore: il runtime legge solo il modello **legacy** a griglia densa scena-livello (`scene-json.cpp:94-148,213-214`) — cioè il lettore canonico punta oggi al modello che il tipo di dominio stesso definisce superato.
- **Campi tileset con nomi/forma diversi**: l'editor scrive `slicing: {tileWidth,tileHeight,marginX,marginY,spacingX,spacingY}` annidato (`project_io.cpp:1301-1316`); il lettore canonico si aspetta chiavi piatte `tileSize`/`tileHeight`/`margin` e non legge affatto `spacingX`/`spacingY` (`project-meta-json.cpp:124-142`) né popola `TilesetAsset.tiles` pur essendo un campo dello struct condiviso (`types.h:595`).
- **Asset senza lettore canonico**: Generated SFX (`doc.generatedSfx`) e Font Assets (`doc.fontAssets`) — nessuna funzione `read_*` esiste lato runtime per nessuno dei due; `fontAssets` viene letto solo come mappa per il manifest, mai popolato su `out.fontAssets`.
- **Specificità degli errori**: il validator canonico ritorna un solo messaggio (primo errore incontrato); sotto di esso, `read_*` usa `.value(key, default)` pervasivamente, che maschera "campo assente" da "campo presente col default" — esattamente la distinzione che l'effort P0-03 della remediation roadmap esistente aveva costruito lato editor. Un mismatch di tipo profondo (es. un campo numerico scritto come stringa) può oggi propagarsi fino a un `catch (...) { return false; }` in `AssetLoader::parseProjectJson`/`loadDirectory` **senza alcun messaggio**.

**Scope**

- Allineare `kCurrentProjectFormatVersion` del runtime alla versione corrente dell'editor (bump a `9`); nessuna migrazione lato runtime è necessaria — il gioco esportato carica sempre l'ultimo formato scritto dall'editor, non file storici (la migrazione resta responsabilità esclusiva dell'editor, come oggi).
- Cambiare il writer dell'editor per scrivere `scenes` come oggetto chiave-per-id (più coerente anche con `entities`, che è già una mappa) invece di array; emettere sempre `globalVariables` (anche vuoto).
- Aggiungere lettori canonici mancanti lato runtime: `boxCollider2D`, tilemap per-istanza a chunk (il modello corrente, non quello legacy), `slicing` annidato del tileset incl. `spacingX`/`spacingY`, `TilesetAsset.tiles`, Generated SFX, Font Assets.
- Decisione esplicita di design sulla specificità degli errori: la validazione ricca "campo per campo" resta un compito del layer Command/UI dell'editor (già presente da P0-02/P0-03), non del parser JSON di boundary — il parser canonico resta la fonte di verità sulla *forma* del file, l'editor resta la fonte di verità sulla *qualità dei messaggi* mostrati durante l'authoring. Documentare questa scelta esplicitamente per evitare che RU-01 provi a "recuperare" specificità che il parser canonico non è progettato per dare.

**Design richiesto**

- [x] Bump `kCurrentProjectFormatVersion` a `9` in `runtime-cpp/src/core/project-current-format.h`, con test di regressione che verifica che un file `formatVersion: 8` non aggiornato venga esplicitamente rifiutato (nessuna migrazione silenziosa lato runtime).
- [x] `validate_current_project_json`/`read_scenes_map` accettano `scenes` come oggetto (già lo facevano — solo il writer editor scriveva l'array; il reader editor accettava già entrambe le forme). Writer editor allineato a oggetto chiave-per-id.
- [x] Editor scrive sempre `globalVariables` (serializzato da `doc.globalVariables`, oggi sempre vuoto in assenza di UI di authoring — nessuno stub hardcoded).
- [x] Nuovo `boxCollider2D` in `read_optional_gameplay_components` (`entity-json.cpp`).
- [x] Nuovo lettore per il tilemap per-istanza a chunk in `read_scene_instance` (`scene-json.cpp`) — il lettore legacy scena-livello (`read_tilemap_object`) resta intatto, invariato, non deprecato.
- [x] `read_tileset_asset` aggiornato a `slicing` annidato con `spacingX`/`spacingY` (con fallback al layout piatto legacy se `slicing` assente), e popola `TilesetAsset.tiles`.
- [x] Nuovo `read_font_assets` (`asset-json.cpp`), wired in `AssetLoader::parseProjectJson`.
- [!] `read_generated_sfx` — **deferito, non implementato in questa slice**. Richiederebbe una nuova dipendenza `artcade-project-json`/`artcade-asset-system` → modulo `sfx` (per `artcade::sfx::deserializeRecipeJson`) che oggi non esiste — un cambiamento di layering architetturale, non una semplice aggiunta meccanica. Gap noto e documentato: se un progetto con SFX generati passa dal loader canonico, `doc.generatedSfx` resta vuoto (gli `AudioAssetDef` risultanti dalla sintesi restano intatti tramite `read_audio_assets`/`generatedFromSfxId`, che sono già letti — si perde solo la *ricetta* editabile, non l'audio già generato).

**Test obbligatori**

- [x] Un progetto salvato dall'editor con lo schema aggiornato passa `validate_current_project_json` senza errori.
- [x] Round-trip dedicato per boxCollider2D/tilemap a chunk/tileset slicing+tiles/fontAssets (`test_ru01a_component_readers_round_trip`, `artcade-package-test.cpp`).
- [x] Test di rifiuto esplicito per `formatVersion: 8` (`test_stale_format_version_rejected`).
- [x] Nessuna regressione: editor 2563+ core-test (+ tutte le altre suite) verdi, runtime-cpp 48/48 CTest al 100%.

**Gate di uscita**

- [x] Un file scritto dall'editor con lo schema corrente passa `AssetLoader::parseProjectJson` producendo uno `ProjectDoc` che preserva `boxCollider2D`, tilemap a chunk, tileset slicing/tiles, font — **eccetto** i SFX generati (gap noto, vedi sopra).

**Commit**: `runtime-cpp` `75a9305e` (feature/runtime-unification), editor `922be4f` (feature/runtime-unification).

## 8. Fase RU-01 — Parser `ProjectDocument` unificato

**Problema**

`project_io.cpp` (~2200 righe) è un parser JSON indipendente da quello canonico del runtime. Se i dati letti dall'editor possono divergere da quelli letti dal gioco esportato, ogni lavoro successivo sulla simulazione condivisa è vano: l'editor potrebbe autorare stati che il runtime canonico rifiuta o interpreta diversamente. Dipende da RU-01a: prima della riconciliazione schema, delegare la lettura significherebbe perdere silenziosamente `boxCollider2D`, tilemap, tileset slicing, SFX generati e font, oltre a rifiutare ogni file esistente per `formatVersion`/`scenes`/`globalVariables`.

**Scope**

- Percorso di lettura di `ProjectDocument` (load): delegare a `ProjectJson::validate_current_project_json`, `read_project_header`, `read_object_types_map`, `read_entities_map`, `read_scenes_map` ecc. (target `artcade-project-json`, già linkabile via la junction) — ora che RU-01a garantisce copertura completa dei campi editor.
- Percorso di scrittura resta di competenza dell'editor (è l'autorità di authoring, nessun writer canonico esiste lato runtime — confermato: zero funzioni `write_*`/`to_json` in `runtime-cpp/src/core`), ma deve validare contro le stesse costanti di schema/versione usate dal validator canonico.
- Per design (vedi RU-01a): la validazione ricca campo-per-campo resta al layer Command/UI dell'editor; il parser canonico diventa l'unica fonte di verità sulla *forma* del documento, non sulla qualità dei messaggi d'errore mostrati durante l'authoring.

**Design richiesto**

- [ ] Mappare ogni funzione di lettura in `project_io.cpp` sulla corrispondente funzione `ProjectJson::*` (mappatura già coperta da RU-01a per i campi che oggi non hanno equivalente).
- [ ] Sostituire la lettura ad-hoc con chiamate al parser canonico.
- [ ] Verificare che tutte le eccezioni JSON restino intercettate al confine pubblico del deserializer (stesso standard di `P0-03` nella remediation roadmap esistente) — nota: `AssetLoader::parseProjectJson` oggi cattura con un generico `catch (...) { return false; }` a livello di `loadDirectory`/`loadArtcade` (`asset-loader.cpp:106-112,135-141`), senza messaggio; l'editor deve avvolgere la chiamata con un boundary che produca comunque un errore mostrabile, anche se meno specifico di prima (vedi decisione di design in RU-01a).
- [ ] Eseguire un test di round-trip: ogni progetto di test esistente (incl. asset ostili/malformati usati nei test P0-03/P0-04) deve produrre lo stesso `ProjectDoc` prima e dopo la sostituzione.

**Test obbligatori**

- [ ] Round-trip su tutti i progetti fixture esistenti (editor + eventuali fixture del runtime).
- [ ] Documento malformato/ostile → errore esplicito mostrabile all'utente (accettabile se meno specifico di prima, mai silenzioso — nessuna regressione sul principio di P0-03/P0-04, anche se il messaggio esatto può cambiare).
- [ ] Nessuna regressione nella suite `editor-core-test`.

**Gate di uscita**

`ProjectDocument` e `AssetLoader::parseProjectJson` leggono lo stesso file producendo dati equivalenti; zero divergenze note tra i due percorsi di lettura.

## 9. Fase RU-02 — Estrazione `GameplaySession` riutilizzabile dal runtime

> **Piano di dettaglio congelato**: [RU02_GAMEPLAY_SESSION_REFACTOR.md](RU02_GAMEPLAY_SESSION_REFACTOR.md). Lo studio iniziale sotto ("l'estrazione è più additiva del previsto") resta corretto per i moduli individuali (`World`/`gateway`/`Physics`/ecc. già testabili headless), ma una lettura più approfondita di `app_bootstrap.cpp`/`app_loop.cpp` ha mostrato che il vero composition root (`Application::initSubsystems()`) intreccia questi moduli con Renderer/Dialog/EditorAPI attraverso un unico `EngineContext` condiviso — costruire `GameplaySession` come composizione parallela rischierebbe di divergere silenziosamente da `Application`. La decisione congelata è quindi **estrarre per refactor da `Application` stesso**, non ricostruire accanto: vedi il documento dedicato per le 8 sotto-fasi (RU-02a→RU-02h), le clausole non negoziabili e il debt register.

**Problema**

`Application::Modules` e il ciclo `Application::loopIteration()`/`tickFixedStep()` (`runtime-cpp/src/app/src/app_loop.cpp:81-297`) assumono di possedere l'intero processo standalone: finestra, polling input OS, audio device. Per essere riutilizzati in-process dall'editor (modello Unity PlayMode / Unreal PIE, non modello Godot a processo separato — coerente con la UX attuale di Play inline nella scene view) serve un building block che possieda la simulazione (World, gateway, physics, Logic, Script, camera) ma **non** l'ownership di finestra/input OS/audio device, che restano a carico di ciascun host.

**Scoperta dallo studio di dettaglio (2026-07-20): l'estrazione è molto più additiva del previsto**

Grep sistematico di chiamate raylib in ogni modulo (`InitWindow`, `IsKeyDown`, `InitAudioDevice`, `LoadTexture`, ecc.) più verifica incrociata con i test esistenti di `runtime-cpp`:

- **Già componibili headless, zero dipendenza raylib, già dimostrato dai test esistenti**: `World`, `RuntimeEntityGateway`, `SceneManager`/`SceneLifecycleService`/`SceneMutationService`, `Physics`, `VariableManager`, `EventBus`, `TimeManager`, `TweenManager`, `SaveLoadManager`, `CameraManager`, `GameStateManager`, `LogicRuntime`+`LuaHost`, `ScriptRuntime`, `GameAPI`, `SpriteAnimator`. Esempio concreto: `tests/runtime-logic-host-parity-test.cpp` costruisce già `SceneManager`+`RuntimeEntityGateway`+`Physics`+`VariableManager`+`World` senza `Application` né finestra. Altri esempi: `tests/world-tilemap-collision-test.cpp`, `tests/scene-lifecycle-test.cpp`, `tests/scene-restart-test.cpp`, `tests/scene-gateway-test.cpp`, `tests/logic-board-test.cpp`, `tests/game-api-state-test.cpp`, più le suite dedicate `camera-manager-test.cpp`/`tween-manager-test.cpp`/`time-manager-test.cpp`/`event-bus-test.cpp`/`game-state-test.cpp`/`save-load-test.cpp`.
- **Genuinamente accoppiati a OS/GPU/audio, refactor reale necessario**:
  - `Input` (`runtime-cpp/src/modules/input/src/input.cpp:46-58,92`) — `poll()` chiama `IsKeyDown`/`IsKeyPressed`/`GetMousePosition` direttamente, nessun seam esistente; `InputState` (`input.h:39`) espone solo un accessor const, nessun setter. Sotto questo confine, `Scripts::ScriptInputSnapshot` e `LogicRuntime::dispatchKeyPressed/Released/Held` sono già agnostici rispetto alla fonte — bastano 3 booleani per `LogicKey` a frame.
  - `Audio` (`runtime-cpp/src/modules/audio/src/audio.cpp:45-50`) — `init()` chiama `InitAudioDevice()` incondizionatamente; il device raylib è un singleton di processo. Un host che possiede già la propria `Audio` (l'editor) non può inizializzarne una seconda per `GameplaySession` senza modificare il modulo per accettare un device condiviso/esterno.
  - `Renderer`/`editorViewport` — deliberatamente fuori scope (la presentazione resta separata per design).
  - `AssetLoader` — API pubblica solo `loadArtcade(path, doc)`/`loadDirectory(path, doc)`, nessun ingresso "ProjectDoc già in memoria"; ma la metà "doc → world/logic/script/scena" di `Application::loadProject` (`app_project_lifecycle.cpp:201-361`, es. righe 220,306,308-309,311,316-332) opera già puramente sul `ProjectDoc` in memoria — riusabile verbatim una volta che RU-01a garantisce che quel `ProjectDoc` sia completo.
- **Rischio per l'eseguibile spedito (`game.exe`)**: nullo per la parte "componibile" (estrazione additiva, non tocca una riga di codice eseguita da `game.exe` oggi); il rischio esiste solo quando si aggiungono i seam a `Input`/`Audio`, perché quei moduli sono linkati direttamente da `game.exe` — qualunque refactor lì deve preservare esattamente i call site OS-facing esistenti (`app_loop.cpp:232,254-256`, `audio.cpp:45-50`), non sostituirli.

**Scope (cross-repo: `runtime-cpp`)**

- Estrarre da `Application::Modules`/`app_loop.cpp` un tipo `GameplaySession` (nome indicativo) che possiede il sottoinsieme già headless-componibile elencato sopra, costruito da `ProjectDoc` riusando la metà doc-consuming di `loadProject`.
- Aggiungere un seam di iniezione input a `Input` (nuovo metodo/struct per impostare lo stato pressed/released/held del frame dall'esterno, senza toccare i call site raylib esistenti di `Application`).
- Rendere `Audio::init()` capace di condividere un device esterno già inizializzato invece di richiamare `InitAudioDevice()` incondizionatamente (o documentare esplicitamente, se non fattibile in questa slice, che l'editor deve riusare la propria istanza `Audio` esistente invece di istanziarne una seconda per `GameplaySession`).
- L'eseguibile standalone (`src/app`) diventa un host sottile che possiede finestra/OS-input/audio-device e delega la simulazione a `GameplaySession`.
- Nessuna riscrittura dei sistemi (`World::tick*`, physics, collision, camera-manager): solo separazione tra "ownership OS" e "ownership simulazione".

**Design richiesto**

- [ ] `GameplaySession` espone: costruzione da `ProjectDoc`, tick a passo fisso equivalente a `tickFixedStep(dt)`, accesso in lettura al gateway per il rendering, il nuovo varco di iniezione input.
- [ ] Seam di iniezione input in `Input` che non tocca i call site raylib esistenti di `Application`.
- [ ] Audio: device condivisibile o riuso esplicito dell'istanza dell'host, documentato.
- [ ] Nessuna modifica al comportamento dell'eseguibile standalone: stesso identico frame sequence, solo ridistribuito tra host e `GameplaySession`.

**Test obbligatori**

- [ ] Tutti i test esistenti di `runtime-cpp` (world, physics, save/load, scene lifecycle, gateway, collisioni, package `.artcade`) restano verdi senza modifiche al loro contenuto.
- [ ] Nuovo test: `GameplaySession` costruita e tickata senza una finestra OS reale (headless), sul modello già usato da `runtime-logic-host-parity-test.cpp`.
- [ ] Nuovo test: iniezione input via il nuovo seam produce lo stesso `dispatchKeyPressed/Released/Held` di un polling raylib equivalente.

**Gate di uscita**

L'eseguibile standalone si comporta in modo osservabilmente identico a prima (nessuna regressione), e `GameplaySession` è istanziabile e tickabile senza possedere finestra/input OS.

## 10. Fase RU-03 — `EditorGameplayRuntimeHost` sostituisce lo stub

> **Superseduta/assorbita da [RU02_GAMEPLAY_SESSION_REFACTOR.md](RU02_GAMEPLAY_SESSION_REFACTOR.md) §7 e §14 (D-01)**: il piano congelato tratta RU-03 come "integrazione Editor Play con `GameplaySession`" a tutto tondo — `EditorGameplayRuntimeHost`/`RuntimeLogicHostAdapter` restano il riferimento per lo spawn (contenuto sotto ancora valido), ma il gate di uscita reale è l'eliminazione di `EditorNative::PlaySession` come secondo runtime (D-01), non solo la sostituzione dello stub. La tabella di delega metodo-per-metodo sotto resta la spec di implementazione corretta per la parte adapter.

**Problema**

`PlaySession::LogicHostAdapter::spawnObjectType` (`play_session.cpp:149-153`) ritorna sempre `INVALID_ENTITY`. La seam per correggerlo esiste già (`IGameplayRuntimeHost`), e la sua implementazione reale (`RuntimeLogicHostAdapter`, `runtime-cpp/src/app/src/app_modules.h:43-191`, `static_assert` a riga 193-194) è il riferimento diretto.

**Spec di implementazione (studio di dettaglio, 2026-07-20 — questa slice è "shovel-ready")**

`IGameplayRuntimeHost` (`gameplay-runtime-host.h:13-69`) ha 24 metodi virtuali puri; `RuntimeLogicHostAdapter` li implementa tutti come thin delegation verso i moduli reali — nessuna logica propria oltre a guardie di precondizione:

| Metodo | Delega a |
|---|---|
| `setVisible`/`isVisible` | `gateway_.setRuntimeVisible`/`visibleInGame` |
| `setPosition`/`translate`/`setRotation`/`rotateBy`/`setScale` | get/set transform via `gateway_`, con guardie `isfinite` |
| `isGrounded`/`isFalling` | `world_->isPlatformerGrounded`/`isPlatformerFalling` |
| `requestPlatformerMove`/`requestPlatformerJump` | valida presenza `PlatformerControllerComponent` via gateway, poi `world_->setMovementIntent`/`requestJump` |
| `isObjectType` | `world_->isObjectType` |
| `requestDestroy` | `world_->requestDestroy` |
| `playAnimationClip`/`stopAnimation`/`setAnimationPlaybackSpeed` | `world_->playAnimationClip`/`stopAnimation`/`setAnimationPlaybackSpeed` |
| `playSound` | guardia `world_->isActiveEntity`, poi `audio_.playResolvedAsset` |
| `setStateNumber`/`addStateNumber`/`toggleStateBoolean`/`getStateNumber` | `variables_->setGlobal`/`addNumber`/`toggleBoolean`/`tryGetNumber` |
| `setVelocity` | transform via gateway, più `physics_->setLinearVelocity` se l'entità ha un handle fisico attivo |
| `isKeyDown` | `input_->isKeyDown(Logic::logicInputCode(key))` |
| `spawnObjectType` | vedi sotto |

**`spawnObjectType` passo-passo** (`app_modules.h:168-181`): (1) guardie — `world_` non nullo, `owner` attivo, `objectTypeId` non vuoto, `x`/`y` finiti; (2) `gateway_.spawnFromClass(objectTypeId, x, y)` — tutta la creazione reale (lookup prototipo, registrazione ECS) vive nel gateway, non nell'adapter; (3) se fallisce, propaga `INVALID_ENTITY`; (4) altrimenti invoca `spawnInstaller_(spawned)` se impostato (`std::function<bool(EntityId)>`, iniettato via `setSpawnInstaller`) — se l'installer fallisce, distrugge l'orfano (`gateway_.destroy`) e ritorna `INVALID_ENTITY`; (5) altrimenti ritorna `spawned`. Lato runtime, `spawnInstaller_` è agganciato a `Application::installLogicScopeForEntity` (`app_project_lifecycle.cpp:386-402`): risolve la classe dell'entità, verifica se ha una Logic Board compilata, la installa (`logicRuntime->install`) e dispaccia `Start` owner-scoped.

`RuntimeEntityGateway::spawnFromClass` (`runtime-entity-gateway.cpp:441-473`): cerca `className` in `classPrototypes_` (clona il primo `EntityDef` visto per quella classe al load, azzerando id/physicsHandle e impostando la posizione) o crea un `EntityDef` minimale se non trovato; poi alloca id, applica i componenti al registro ECS, registra la scena attiva e attiva fisica/animator. `classPrototypes_` è popolato **una sola volta al load del progetto**, da `RuntimeEntityGateway::replaceProject` (chiamata da `World::init(doc)`, `world.cpp:191-193`, e dal percorso editor-api, `editor-api.cpp:1085-1087`) — priorità al catalogo `ProjectDoc::objectTypes`, fallback alla prima entità autorata di quella classe in scena.

**Dipendenze minime per `EditorGameplayRuntimeHost`**, dedotte metodo-per-metodo: un gateway equivalente a `RuntimeEntityGateway` (visibilità/transform/physics-handle/controller-check), un `World`-equivalente (grounded/falling/movimento/jump/destroy/animazione/type-check), `Audio` (playSound), `VariableManager` (le 4 variabili di stato), `Input` (isKeyDown), `Physics` (setVelocity), e opzionalmente uno `spawnInstaller` per l'attivazione Logic/Script post-spawn — è esattamente questo il pezzo che manca allo stub attuale dell'editor, che non arriva mai a interpellare l'equivalente di `gateway_.spawnFromClass`.

**Scope**

- Editor: scrivere `EditorGameplayRuntimeHost`, appoggiato alla `GameplaySession`/gateway reale di RU-02 (non a `RuntimeScene`/`RuntimeEntity` di `PlaySession`), seguendo la tabella di delega sopra 1:1.
- Non ancora collegato al Play-start reale dell'editor in questa slice (quello è RU-04): questa slice produce e testa l'adapter in isolamento.

**Design richiesto**

- [ ] `EditorGameplayRuntimeHost` implementa tutti e 24 i metodi di `IGameplayRuntimeHost` seguendo la tabella di delega, incluso `spawnObjectType` reale via `spawnFromClass` + `spawnInstaller` per l'attivazione Logic/Script.
- [ ] `static_assert(!std::is_abstract_v<EditorGameplayRuntimeHost>)` nel punto di definizione, a specchio esatto di `app_modules.h:193-194`.
- [ ] Nessuna logica di gameplay duplicata: l'adapter è un thin wrapper, la logica resta in `World`/gateway.

**Test obbligatori**

- [ ] Test diretto: `spawnObjectType` tramite `EditorGameplayRuntimeHost` produce un'entità valida con i componenti attesi dall'Object Type, incluso l'aggancio Logic Board/Script quando presente.
- [ ] Parità comportamentale con `RuntimeLogicHostAdapter` sugli stessi input di test, dove applicabile (stessa tabella di delega, stessi risultati).

**Gate di uscita**

`EditorGameplayRuntimeHost` non ha metodi stub; uno spawn richiesto da Logic Board o Script tramite questo host produce un'entità reale nel gateway, con Logic/Script installati se l'Object Type ne ha.

## 11. Fase RU-04 — Play-start come "export a temp + load via `AssetLoader`"

**Problema**

Oggi `EditorCoordinator::playProject`/`playCurrentScene` (`editor_coordinator.cpp:623-703`) costruiscono `PlaySession` direttamente dal `ProjectDocument` in memoria. Per garantire parità reale col gioco esportato, Play deve attraversare lo stesso percorso di caricamento del runtime canonico, non solo la stessa simulazione.

**Scope**

- Al Play-start: serializzare il `ProjectDocument` corrente in un pacchetto temporaneo tramite il writer canonico (già allineato a `ProjectJson::*` da RU-01), poi caricarlo tramite `AssetLoader::loadArtcade`/`loadDirectory` reale — lo stesso percorso che userà l'Export (blocco P0-B), anticipandone la fondazione.
- Costruire `GameplaySession` (RU-02) con `EditorGameplayRuntimeHost` (RU-03) a partire da questo caricamento.
- Camera: se la scena non ha una `Camera2D` autorata, seminare il vero `CameraManager` con `computeFitZoom` esistente come profilo di default (non sostituirlo).
- Rendering: la scene view dell'editor legge lo stato renderizzabile dal `RuntimeEntityGateway` reale invece che da `RuntimeScene`.
- Rimuovere il temp package a fine sessione di Play; gestire fallimenti di export-temp con lo stesso standard di errore esplicito già richiesto per il resto del dominio (nessun fallback silenzioso).

**Design richiesto**

- [ ] Percorso Play-start: `ProjectDocument` → writer canonico → temp package → `AssetLoader` → `GameplaySession(EditorGameplayRuntimeHost)`.
- [ ] Nessuna scrittura su disco permanente: directory temporanea gestita e ripulita dal ciclo di vita di Play (analogo a come già si gestiscono le risorse di `PlaySession` oggi).
- [ ] Isolamento camera preservato: verificare esplicitamente che il pan/zoom workspace dell'editor non influenzi la camera di Play (stesso vincolo già verificato con `--shot-play`/`--shot-pan`/`--shot-zoom` nella sessione precedente).
- [ ] Input: instradare gli eventi già gestiti dall'editor (tastiera) nell'interfaccia di input di `GameplaySession` definita in RU-02, invece del polling diretto oggi in `PlaySession::isKeyDown`.

**Test obbligatori**

- [ ] `--shot-play`, `--shot-pan`, `--shot-zoom` (esistenti) continuano a passare senza modifiche al comportamento atteso.
- [ ] Nuovo test/shot: uno spawn via Logic Board durante Play nell'editor produce un'entità visibile (prima impossibile per lo stub RU-03).
- [ ] Tempo di avvio di Play resta accettabile per iterazione interattiva (misurare, non solo verificare correttezza) — se il round-trip a file introduce una latenza percepibile, valutare un loader `AssetLoader` in-memory come ottimizzazione successiva, non come requisito di questa slice.

**Gate di uscita**

Play nell'editor usa lo stesso World/gateway/Logic/Script/camera del gioco esportato, caricati tramite lo stesso parser e lo stesso loader; nessuna regressione sui test di isolamento camera esistenti.

## 12. Fase RU-05 — Ritiro di `PlaySession` e codice duplicato

**Problema**

Con RU-04 completo, `PlaySession`, `RuntimeEntity`, `RuntimeScene` e la logica di movimento/collisione hand-rolled (`play_session.cpp:299-327`, `845-895`, `1032-1167`) sono morti: nessun percorso li chiama più.

**Scope**

- Rimuovere `play_session.h/.cpp` e ogni riferimento residuo.
- Rimuovere `PlaySession::LogicHostAdapter` (sostituito da `EditorGameplayRuntimeHost` in RU-03).
- Verificare che nessun test residuo dipenda da `PlaySession` (aggiornare o rimuovere quei test, mai lasciarli a puntare a codice morto).

**Design richiesto**

- [ ] Nessun simbolo `PlaySession`/`RuntimeEntity`/`RuntimeScene` residuo nel repo editor.
- [ ] `src/CMakeLists.txt` aggiornato per rimuovere `play_session.cpp` dai target.

**Test obbligatori**

- [ ] Build pulita senza warning di simboli non referenziati legati alla rimozione.
- [ ] Intera suite `editor-core-test` verde.

**Gate di uscita**

Una sola implementazione di simulazione gameplay esiste nel monorepo logico (editor + runtime), usata sia da Play sia dall'Export.

## 13. GATE-FINAL — Parity audit sui gate games

**Problema**

Il criterio di accettazione ufficiale dell'intera iniziativa, definito nell'audit originale: 3 giochi realizzabili interamente dall'editor devono comportarsi in modo identico tra Editor Play, build Windows esportata e build Web esportata.

**Attività**

- [ ] Gate 1 — Platformer: più livelli, Tilemap collision, moving platform, collectibles, nemici, HUD, checkpoint, gamepad, save.
- [ ] Gate 2 — Top-down action: camera follow, spawn nemici, health, collision layers, animation state, SFX/musica, pause menu, scene transition.
- [ ] Gate 3 — Puzzle/arcade: main menu, variabili globali, timer, punteggio, high score persistente, restart, game over.
- [ ] Per ciascun gate: verificare parità Editor Play = build Windows = build Web sulla stessa semantica osservabile (non solo "non crasha").

**Nota**: i Gate 1-3 richiedono funzionalità non ancora costruite in questa roadmap (game flow/scene change, UI/HUD, Input Actions, save/load — blocchi P0-C/D/E dell'audit originale). GATE-FINAL come descritto qui è quindi il criterio di accettazione a lungo termine per l'intera iniziativa di unificazione, non un gate raggiungibile subito dopo RU-05: da RU-05 in poi, ogni nuova feature gameplay (P0-C in avanti) si costruisce direttamente sul runtime unificato, senza più passare da un adapter editor-only.

**Gate di uscita**

Nessuna divergenza osservabile tra Editor Play e build esportata sui 3 gate games.

## 14. Rischi noti e mitigazioni

| Rischio | Impatto | Mitigazione |
|---|---|---|
| Il loader canonico rifiuta oggi ogni file scritto dall'editor (`formatVersion`/`scenes`/`globalVariables`), verificato non ipotizzato | Blocca RU-01 e RU-04 finché non risolto | RU-01a, nuova slice dedicata, precede entrambe in tabella di dipendenza |
| Componenti editor senza lettore canonico (`boxCollider2D`, tilemap a chunk, tileset slicing, SFX generati, font) verrebbero scartati silenziosamente se si delegasse la lettura prima di colmare il gap | Perdita silenziosa di dati autorati, peggio di un errore esplicito | RU-01a aggiunge i lettori mancanti prima che RU-01 cambi il percorso di lettura reale |
| Delegare al parser canonico riduce la specificità dei messaggi di errore rispetto al parser attuale (P0-03) | Regressione UX per progetti malformati | Decisione di design esplicita in RU-01a: la validazione ricca resta al layer Command/UI, il parser canonico è solo l'autorità sulla forma |
| `Input`/`Audio` assumono ownership diretta di OS/device raylib (RU-02) | Blocca l'embedding in-process se non disaccoppiato | RU-02 introduce un seam di iniezione input e un device audio condivisibile/riusato, senza toccare i call site OS-facing di `game.exe` |
| Latenza del round-trip "temp export" ad ogni Play-start (RU-04) | Iterazione interattiva percepita come lenta | Misurare in RU-04; se necessario, loader `AssetLoader` in-memory come ottimizzazione successiva fuori scope di questa roadmap |
| Regressione dell'isolamento camera Edit/Play già costruito | Riapre un bug già risolto nella sessione precedente | RU-04 riusa esplicitamente `computeFitZoom` come seed, verificato dagli stessi `--shot-*` esistenti |
| Drift tra `runtime-cpp` e la junction durante una migrazione multi-settimana | Editor e runtime esportato tornano a divergere a metà lavoro | RU-00 fissa e documenta il commit puntato dalla junction; ogni slice cross-repo aggiorna esplicitamente il puntamento in un commit dedicato, mai implicito |
| Test del runtime che assumono `Raylib`/`Lua` reali sono placeholder (nota dall'audit originale) | Falsi positivi di parità | GATE-FINAL richiede verifica manuale/screenshot sui 3 gate games, non solo test automatici, finché quei placeholder non sono risolti |
