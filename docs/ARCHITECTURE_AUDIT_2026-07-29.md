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

### P1 — `EditorUi` è un hub di routing troppo grande

`src/editor-native/ui/editor_ui.cpp` contiene 4.643 righe; il suo router `EditorUi::handleAction(...)` riceve azioni string-based e coordina molte aree funzionali. `EditorUi` possiede inoltre il listener RmlUi e conosce tutti i pannelli principali.

Il singolo ingresso RmlUi è corretto. Il problema non è il confine, ma la densità: ogni nuova azione può interagire con focus, pending edit, menù, refresh e policy Play nello stesso punto.

**Rischio:** regressioni incrociate e crescita di un protocollo implicito di stringhe/argomenti.

**Azione richiesta:** estrarre progressivamente router per area (`Hierarchy`, `Inspector`, `Assets`, `Logic`, `Script`) che producano gli stessi Intent/Command. Conservare `EditorUi` come ingress controller e proprietario del lifecycle; non introdurre un event bus generico.

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

