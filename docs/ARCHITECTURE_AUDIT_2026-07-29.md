# Audit architetturale — rischio spaghetti code

**Data:** 2026-07-29  
**Ambito:** repository `ArtCade_Editor_RmlUi`, inclusi i cambiamenti presenti nel worktree al momento dell'audit.  
**Metodo:** lettura della costituzione architetturale, ispezione di target e dipendenze CMake, scansione dei confini di modulo, analisi delle concentrazioni di codice e verifica limitata dei test già compilati.  
**Modifiche effettuate dall'audit:** nessuna al codice di prodotto.

## Verdetto

Il repository **non è spaghetti code**. È un monolite modulare con confini architetturali reali e un modello di mutazione disciplinato.

Il rischio è però **medio-alto**: la crescita funzionale si sta concentrando in pochi file-hub. Senza interventi incrementali, tali hub possono trasformare confini oggi chiari in un intreccio difficile da modificare e verificare.

| Dimensione | Stato | Sintesi |
|---|---|---|
| Autorità dei dati | Verde | `ProjectDocument` è protetto; la UI lo osserva in sola lettura. |
| Mutazioni e Undo | Verde | Il coordinator centralizza Command/Intent e mantiene Undo/Redo. |
| Confini core/UI | Verde | `model` e `commands` non includono RmlUi o Raylib. |
| Complessità locale | Ambra | Alcuni file sono hub molto grandi e con alta densità decisionale. |
| Build e test | Ambra/Rosso | Il manifest CTest e gli artefatti costruiti non coincidono sempre; un test core è risultato flaky. |
| Igiene della configurazione | Ambra | Un CMake legacy descrive un prodotto ormai non rappresentativo. |

## Evidenze positive

### Autorità e percorso di mutazione

`EditorCoordinator::document()` espone un `const ProjectDocument&`; la UI non riceve un riferimento mutabile al dominio. L'esecuzione dei comandi è centralizzata in `EditorCoordinator::execute(...)` e nei relativi Intent/Command.

Questo rispetta i principi AC-AUTH-001 e AC-MUT-001 della costituzione: la RmlUi non diventa uno store applicativo e non muta direttamente il progetto.

Riferimenti:

- `src/editor-native/app/editor_coordinator.h`, righe 86 e 120.
- `src/editor-native/commands/editor_command.h`.
- `docs/ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`, sezioni 4–5 e 8.

### Confini di compilazione

Il CMake root costruisce separatamente:

- `artcade-editor-core`;
- `artcade-editor-ui`;
- `artcade-editor-native`.

La scansione non ha rilevato include RmlUi/Raylib sotto `src/editor-native/model` né sotto `src/editor-native/commands`. Il core resta quindi libero da dipendenze di presentazione/piattaforma, in linea con AC-MOD-001.

Riferimenti:

- `src/CMakeLists.txt`, righe 10, 111, 242, 253 e 274.
- `docs/ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`, sezione 9.

### Test intenzionali ai confini UI

Il repository registra 21 test CTest e include test che attraversano il listener RmlUi reale per casi di focus, inspector e Logic Board. Questo è preferibile a test esclusivamente di funzioni isolate per i rischi presenti al confine RmlUi/editor.

Riferimento: `tests/CMakeLists.txt`.

## Rischi e rilievi

### P1 — lifecycle/test non deterministico in Play

In due invocazioni successive della build già presente, `editor_core_test` ha prima superato il test e poi fallito due assertion nel controllo delle directory temporanee `artcade-play-*`:

```text
FAIL: countScratchDirs() == before (line 2870)
FAIL: countScratchDirs() == before (line 2872)
```

Il test è in `tests/editor-core-test.cpp` e controlla che Play materializzi e pulisca le risorse temporanee senza residui.

Non è possibile attribuire con certezza il difetto alla modifica non committata: gli artefatti eseguiti possono non corrispondere all'intero worktree. Tuttavia il risultato è sufficiente per classificare il gate come non affidabile finché non sia riprodotto su build pulita e ricondotto a una causa.

**Rischio:** perdita di determinismo, cleanup incompleto o contaminazione tra test. Il tema tocca AC-LIFE-001 e AC-PLAY-001.

**Azione richiesta:** riprodurre da build pulita, loggare il path e l'owner di ogni scratch directory, e aggiungere una regressione che verifichi cleanup sia su Start riuscito sia su Start fallito.

### P2 — routing UI: concentrazione residua, non God router

`src/editor-native/ui/editor_ui.cpp` contiene 4.643 righe e `EditorUi` possiede il listener RmlUi e conosce i pannelli principali. Una rilettura successiva ha però confermato che `handleAction(...)` non è più una catena monolitica: è un dispatcher che delega a handler per progetto, console, asset, toolbar, gerarchia e inspector, oltre ai controller dedicati per Logic Board, Script, Sprite Animation e Tileset.

Il singolo ingresso RmlUi è corretto e la suddivisione attuale è un confine semantico reale. Rimane un rischio nel protocollo comune basato su stringhe/argomenti e nelle poche azioni shell condivise che gestiscono menu transitori, focus e pending edit.

**Rischio:** regressioni incrociate se il protocollo di stringhe cresce senza catalogo/contratto coperto da test.

**Azione richiesta:** non estrarre file artificialmente. Quando emergerà una nuova famiglia di azioni, aggiungerla a un handler/controller di dominio esistente e coprirne il contratto RmlUi. Un eventuale passaggio da stringhe a `EditorActionId` va fatto solo dopo avere una copertura verde del gate e senza introdurre un event bus generico.

Riferimenti:

- `src/editor-native/ui/editor_ui.h`, righe 65 e 293.
- `src/editor-native/ui/editor_ui.cpp`, riga 3138.

### P1 — gate CTest non autosufficiente

`tests/CMakeLists.txt` registra test UI quali `logic_expression_focus_routing_test`, ma una invocazione diretta di CTest sulla build presente non trova sempre i rispettivi `.exe`: tali target vengono costruiti in seguito e uno a uno da `scripts/build.bat`.

Lo script di build è quindi più completo di `ctest`, ma il manifest CTest da solo non rappresenta un gate ripetibile.

**Rischio:** una CI o uno sviluppatore che usi solo `ctest` vede falsi fallimenti oppure omette test necessari a seconda dei target costruiti in precedenza.

**Azione richiesta:** aggiungere un target `check`/preset che costruisca tutti i test registrati prima di `ctest --output-on-failure`; in alternativa, usare label CTest per distinguere i test non costruiti di default. Il comando documentato deve coincidere con il gate reale.

Riferimenti:

- `tests/CMakeLists.txt`, righe 92–147.
- `scripts/build.bat`, righe 95 e 144–155.

### P2 — CMake legacy non raggiunto e concettualmente obsoleto

`src/editor-native/CMakeLists.txt` non è incluso dal CMake root, ma definisce nuovamente `artcade-editor-core` e `artcade-editor-native`, parla di editor “spike” e conserva riferimenti a `runtime-cpp`.

**Rischio:** due descrizioni divergenti del prodotto; un manutentore può configurare accidentalmente il tree sbagliato o reintrodurre concetti contrari alla decisione “native RmlUi only”.

**Azione richiesta:** rimuoverlo se non è un entry point supportato; in alternativa sostituirlo con un `message(FATAL_ERROR ...)` che indirizzi esplicitamente al CMake root. Questa è igiene di configurazione, non un refactor del runtime.

Riferimento: `src/editor-native/CMakeLists.txt`.

### P2 — file-hub da dividere per responsabilità, non per dimensione

| File | LOC circa | Valutazione |
|---|---:|---|
| `src/editor-native/ui/editor_ui.cpp` | 4.643 | Router e lifecycle UI: priorità alta. |
| `src/editor-native/model/project_io.cpp` | 2.680 | Serializer: dividere per sezioni schema/migrazioni/validazione. |
| `src/editor-native/app/editor_app.cpp` | 2.355 | Composition root legittima, ma separare setup/lifecycle per area. |
| `src/editor-native/ui/inspector_panel.cpp` | 2.204 | Rendering, menu e draft: estrarre sezioni semanticamente autonome. |
| `src/editor-native/ui/logic_board_panel.cpp` | 1.333 | Da sorvegliare durante l'evoluzione Logic Board. |

La dimensione da sola non è una violazione: `ProjectSerializer` e composition root hanno per natura molte responsabilità coordinate. La divisione va fatta solo quando chiarisce ownership, invarianti o lifecycle, come richiede AC-SIMPLE-001.

## Piano di rientro consigliato

1. **Bloccare la flakiness Play.** Riproduzione pulita, ownership esplicita delle scratch directory, test di cleanup sui failure path.
2. **Rendere il gate unico.** Un comando/preset che costruisce ed esegue esattamente tutti i test dichiarati.
3. **Eliminare il CMake non supportato.** Nessuna seconda entry point ambigua.
4. **Spezzare il router UI per feature.** Una slice alla volta, con test sul listener reale per ogni comportamento migrato.
5. **Modularizzare serializer e inspector solo sulle frontiere semantiche.** Ad esempio formato corrente, migrazioni, validazione e I/O; non creare manager generici.

## Criterio di riesame

Rieseguire questo audit quando sarà completato il punto 1 o quando `EditorUi::handleAction` crescerà ulteriormente. Il successo non consiste nell'aumentare il numero di file, ma nel mantenere dimostrabili:

- un'unica autorità dei dati;
- mutazioni atomiche e annullabili;
- Edit/Play isolati;
- lifecycle deterministico;
- un build/test gate ripetibile;
- dipendenze core → UI impossibili per costruzione.

## Stato di attuazione — 2026-07-29

Questa sezione registra le azioni intraprese dopo l'audit. Non sostituisce i
rilievi precedenti: un punto è chiuso soltanto quando la verifica pertinente è
verde.

| Azione | Stato | Evidenza |
|---|---|---|
| Ownership scratch directory Play | Implementata, verifica completa del gate pendente | `PlaySession` crea una directory esclusiva con `create_directory`, non adotta residui PID/counter, e rende Start Play fallibile se il cleanup finale non riesce. |
| Audio process-global di Play | Implementata | `PlaySession` richiede esclusivamente un device audio già aperto dall'host; nei test headless Play resta silenzioso e non inizializza miniaudio. |
| Gate CTest autosufficiente | Implementato | Il target `artcade-editor-tests` dipende da tutti i 21 eseguibili registrati; `build.bat --test` lo costruisce e poi invoca CTest. |
| Directory di lavoro CTest | Implementata | Ogni test viene eseguito dalla root del repository, come avveniva già nello script, evitando falsi fallimenti di fixture relative. |
| Entry point CMake legacy | Chiuso | `src/editor-native/CMakeLists.txt` fallisce intenzionalmente e rinvia al root CMake. |
| Decomposizione router UI | P1 chiuso dopo riesame | I router di dominio e i controller dedicati esistono già; una divisione fisica ulteriore non ridurrebbe un rischio concreto. Rimane P2 il protocollo string-based condiviso. |
| Modularizzazione serializer | Non avviata intenzionalmente | Non è sicuro iniziare un refactor strutturale mentre il gate runtime non è affidabile. |

### Verifica eseguita dopo le modifiche

- La riconfigurazione dal root CMake è riuscita.
- `cmake --build build --target artcade-editor-tests` ha costruito anche i test
  UI precedentemente assenti dagli artefatti CTest.
- Il file generato `build/tests/CTestTestfile.cmake` dichiara ora
  `WORKING_DIRECTORY` uguale alla root del repository per tutti i test.
- La configurazione diretta di `src/editor-native` fallisce con il messaggio
  previsto, prima di costruire un grafo alternativo.

La suite completa non è ancora dichiarabile verde. L'isolamento audio ha
eliminato le inizializzazioni miniaudio dal percorso Play headless, ma
`editor_core_test` continua a fallire in casi preesistenti di
migrazione/serializzazione e New Project quando è eseguito dalla root del
repository, che è anche la directory usata da `build.bat`. Tali failure devono
essere riprodotte e corrette prima di procedere con refactor strutturali. Non
vanno mascherate con timeout, cambi di working directory opportunistici o
indebolimento dei test.
