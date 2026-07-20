# ArtCade RmlUi — Play/Export Runtime Unification Roadmap

**Stato:** pianificato
**Ambito:** editor desktop nativo (`ArtCade_Editor_RmlUi`) e runtime condiviso (`ArtCade-Studio_V2/runtime-cpp`, collegato tramite junction `vendor/artcade-runtime`)
**Scopo:** eliminare la doppia simulazione Editor Play / gioco esportato (blocco P0-A dell'audit tecnico) e riusare lo stesso lavoro come fondazione per l'Export (blocco P0-B)
**Baseline:** audit tecnico + investigazione architetturale del 2026-07-20 (vedi §2)
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
| RU-01 | P0 | Parser `ProjectDocument` unificato sul canonico `ProjectJson::*` | [ ] | RU-00 | editor |
| RU-02 | P0 | Estrazione `GameplaySession` riutilizzabile dal runtime | [ ] | RU-00 | runtime (+editor a valle) |
| RU-03 | P0 | `EditorGameplayRuntimeHost` sostituisce lo stub `spawnObjectType` | [ ] | RU-02 | editor, runtime |
| RU-04 | P0 | Play-start come "export a temp + load via `AssetLoader`" | [ ] | RU-01, RU-03 | editor |
| RU-05 | P0 | Ritiro di `PlaySession`/`RuntimeEntity`/`RuntimeScene` e codice duplicato | [ ] | RU-04 | editor |
| GATE-FINAL | Gate | Parity audit sui 3 gate games, Windows + Web | [ ] | RU-05 | editor, runtime |

Stati ammessi: `[ ]` non iniziato, `[-]` in corso, `[x]` completato, `[!]` bloccato con motivazione registrata.

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

## 7. Fase RU-01 — Parser `ProjectDocument` unificato

**Problema**

`project_io.cpp` (~2200 righe) è un parser JSON indipendente da quello canonico del runtime. Se i dati letti dall'editor possono divergere da quelli letti dal gioco esportato, ogni lavoro successivo sulla simulazione condivisa è vano: l'editor potrebbe autorare stati che il runtime canonico rifiuta o interpreta diversamente. Va chiuso per primo, a basso rischio (non tocca la simulazione).

**Scope**

- Percorso di lettura di `ProjectDocument` (load): delegare a `ProjectJson::validate_current_project_json`, `read_project_header`, `read_object_types_map`, `read_entities_map`, `read_scenes_map` ecc. (target `artcade-project-json`, già linkabile via la junction).
- Percorso di scrittura resta di competenza dell'editor (è l'autorità di authoring), ma deve validare contro le stesse costanti di schema/versione usate dal writer canonico, per garantire che ciò che l'editor scrive sia sempre leggibile dal loader canonico del runtime.

**Design richiesto**

- [ ] Mappare ogni funzione di lettura in `project_io.cpp` sulla corrispondente funzione `ProjectJson::*`.
- [ ] Sostituire la lettura ad-hoc con chiamate al parser canonico, preservando i messaggi di errore già mostrati in UI (o migliorandoli, mai peggiorandoli).
- [ ] Verificare che tutte le eccezioni JSON restino intercettate al confine pubblico del deserializer (stesso standard di `P0-03` nella remediation roadmap esistente).
- [ ] Eseguire un test di round-trip: ogni progetto di test esistente (incl. asset ostili/malformati usati nei test P0-03/P0-04) deve produrre lo stesso `ProjectDoc` prima e dopo la sostituzione.

**Test obbligatori**

- [ ] Round-trip su tutti i progetti fixture esistenti (editor + eventuali fixture del runtime).
- [ ] Documento malformato/ostile → stesso comportamento di errore esplicito (nessuna regressione su P0-03/P0-04 della remediation roadmap).
- [ ] Nessuna regressione nella suite `editor-core-test`.

**Gate di uscita**

`ProjectDocument` e `AssetLoader::parseProjectJson` leggono lo stesso file producendo dati equivalenti; zero divergenze note tra i due percorsi di lettura.

## 8. Fase RU-02 — Estrazione `GameplaySession` riutilizzabile dal runtime

**Problema**

`Application::Modules` e il ciclo `Application::loopIteration()`/`tickFixedStep()` (`runtime-cpp/src/app/src/app_loop.cpp:81-297`) assumono di possedere l'intero processo standalone: finestra, polling input OS, audio device. Per essere riutilizzati in-process dall'editor (modello Unity PlayMode / Unreal PIE, non modello Godot a processo separato — coerente con la UX attuale di Play inline nella scene view) serve un building block che possieda la simulazione (World, gateway, physics, Logic, Script, camera) ma **non** l'ownership di finestra/input OS/audio device, che restano a carico di ciascun host.

**Scope (cross-repo: `runtime-cpp`)**

- Estrarre da `Application::Modules`/`app_loop.cpp` un tipo `GameplaySession` (nome indicativo) che espone: costruzione da `ProjectDoc` + asset caricati, un metodo di tick a passo fisso equivalente a `tickFixedStep(dt)`, accesso in lettura al gateway per il rendering, un varco per iniettare eventi di input (invece di pollare l'OS direttamente).
- L'eseguibile standalone (`src/app`) diventa un host sottile che possiede finestra/OS-input/audio-device e delega la simulazione a `GameplaySession`.
- Nessuna riscrittura dei sistemi (`World::tick*`, physics, collision, camera-manager): solo separazione tra "ownership OS" e "ownership simulazione".

**Design richiesto**

- [ ] Definire l'interfaccia di input che `GameplaySession` consuma (eventi pressed/released/held), disaccoppiata dal polling OS diretto oggi presente in `Application`.
- [ ] Verificare che l'audio non assuma un singolo device globale non condivisibile — documentare esplicitamente come l'host (editor) inietterà i comandi audio.
- [ ] Nessuna modifica al comportamento dell'eseguibile standalone: stesso identico frame sequence, solo ridistribuito tra host e `GameplaySession`.

**Test obbligatori**

- [ ] Tutti i test esistenti di `runtime-cpp` (world, physics, save/load, scene lifecycle, gateway, collisioni, package `.artcade`) restano verdi senza modifiche al loro contenuto.
- [ ] Nuovo test: `GameplaySession` costruita e tickata senza una finestra OS reale (headless), a dimostrazione dell'effettiva separazione di ownership.

**Gate di uscita**

L'eseguibile standalone si comporta in modo osservabilmente identico a prima (nessuna regressione), e `GameplaySession` è istanziabile e tickabile senza possedere finestra/input OS.

## 9. Fase RU-03 — `EditorGameplayRuntimeHost` sostituisce lo stub

**Problema**

`PlaySession::LogicHostAdapter::spawnObjectType` (`play_session.cpp:149-153`) ritorna sempre `INVALID_ENTITY`. La seam per correggerlo esiste già (`IGameplayRuntimeHost`), e la sua implementazione reale (`RuntimeLogicHostAdapter`, `app_modules.h:43-191`, con `static_assert(!std::is_abstract_v<...>)` a riprova che nessun metodo è stub) è il riferimento diretto.

**Scope**

- Editor: scrivere `EditorGameplayRuntimeHost`, analogo a `RuntimeLogicHostAdapter`, appoggiato alla `GameplaySession`/`RuntimeEntityGateway` reale invece che a `RuntimeScene`/`RuntimeEntity` di `PlaySession`.
- Non ancora collegato al Play-start reale dell'editor in questa slice (quello è RU-04): questa slice produce e testa l'adapter in isolamento, verificandolo contro `IGameplayRuntimeHost` con lo stesso `static_assert` usato lato runtime.

**Design richiesto**

- [ ] `EditorGameplayRuntimeHost` implementa ogni metodo di `IGameplayRuntimeHost` senza stub (incluso `spawnObjectType` reale, tramite `RuntimeEntityGateway::spawnFromClass` o equivalente).
- [ ] `static_assert(!std::is_abstract_v<EditorGameplayRuntimeHost>)` nel punto di definizione, a specchio del vincolo già presente lato runtime.
- [ ] Nessuna logica di gameplay duplicata: l'adapter è un thin wrapper, la logica resta in `World`/gateway.

**Test obbligatori**

- [ ] Test diretto: `spawnObjectType` tramite `EditorGameplayRuntimeHost` produce un'entità valida con i componenti attesi dall'Object Type.
- [ ] Parità comportamentale con `RuntimeLogicHostAdapter` sugli stessi input di test, dove applicabile.

**Gate di uscita**

`EditorGameplayRuntimeHost` non ha metodi stub; uno spawn richiesto da Logic Board o Script tramite questo host produce un'entità reale nel gateway.

## 10. Fase RU-04 — Play-start come "export a temp + load via `AssetLoader`"

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

## 11. Fase RU-05 — Ritiro di `PlaySession` e codice duplicato

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

## 12. GATE-FINAL — Parity audit sui gate games

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

## 13. Rischi noti e mitigazioni

| Rischio | Impatto | Mitigazione |
|---|---|---|
| `World`/gateway assumono ownership di input OS/audio device (RU-02) | Blocca l'embedding in-process se non disaccoppiato | RU-02 introduce esplicitamente un varco di iniezione input prima di toccare l'editor |
| Latenza del round-trip "temp export" ad ogni Play-start (RU-04) | Iterazione interattiva percepita come lenta | Misurare in RU-04; se necessario, loader `AssetLoader` in-memory come ottimizzazione successiva fuori scope di questa roadmap |
| Regressione dell'isolamento camera Edit/Play già costruito | Riapre un bug già risolto nella sessione precedente | RU-04 riusa esplicitamente `computeFitZoom` come seed, verificato dagli stessi `--shot-*` esistenti |
| Drift tra `runtime-cpp` e la junction durante una migrazione multi-settimana | Editor e runtime esportato tornano a divergere a metà lavoro | RU-00 fissa e documenta il commit puntato dalla junction; ogni slice cross-repo aggiorna esplicitamente il puntamento in un commit dedicato, mai implicito |
| Test del runtime che assumono `Raylib`/`Lua` reali sono placeholder (nota dall'audit originale) | Falsi positivi di parità | GATE-FINAL richiede verifica manuale/screenshot sui 3 gate games, non solo test automatici, finché quei placeholder non sono risolti |
