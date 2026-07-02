# ArtCade — refactor controllato verso editor nativo C++ con RmlUi

Agisci come principal software architect e senior C++ engineer sul repository ArtCade.

Devi progettare e implementare uno **spike architetturale concreto, isolato e compilabile** per valutare la sostituzione futura dell’editor React/WebView/WASM con un editor nativo C++ basato su RmlUi.

Non devi riscrivere immediatamente l’intero editor.

La priorità assoluta non è portare il maggior numero possibile di pannelli. La priorità è dimostrare che l’editor nativo può essere:

- più semplice;
- più lineare;
- deterministico;
- privo di sincronizzazioni distribuite;
- privo di lifecycle asincroni non necessari;
- privo di stato duplicato tra UI e runtime;
- direttamente integrato con il core C++ esistente;
- sufficientemente rifinito visivamente da sostituire in futuro la UI web.

Il repository corrente ha già un backup esterno, ma trattalo comunque come codice di produzione.

---

# 1. Obiettivo finale dello spike

Creare un nuovo executable parallelo, indicativamente:

```text
artcade-editor-native
```

basato su:

```text
C++
+ RmlUi
+ finestra/rendering nativo esistente
+ viewport ArtCade reale
```

Il nuovo target deve funzionare senza:

- React;
- TypeScript UI;
- DOM browser;
- WebView2;
- JavaScript;
- WASM nel processo editor;
- bridge JSON tra UI e core;
- servizi di scene sync;
- readiness flag distribuiti;
- fingerprint per sincronizzare lo stato;
- polling dello stato authoring.

L’editor web attuale deve restare disponibile e compilabile per tutta la durata dello spike.

Lo spike deve dimostrare questa catena minima:

```text
native window
→ RmlUi shell
→ input deterministico
→ EditorCoordinator
→ ProjectDocument / stato authoring
→ viewport ArtCade reale
→ clean shutdown
```

---

# 2. Principio architetturale fondamentale

Il nuovo editor deve avere un solo flusso di modifica.

```text
evento RmlUi
→ EditorController
→ EditorCommand oppure EditorIntent
→ EditorCoordinator
→ modifica dello stato autorevole
→ invalidazione esplicita
→ aggiornamento dei soli consumer interessati
→ frame successivo
```

Non introdurre meccanismi automatici di riconciliazione generale.

Non ricreare con RmlUi gli stessi problemi già incontrati con React.

Sono vietati come architettura ordinaria:

- sync bidirezionale;
- servizi generici di sincronizzazione;
- observer globali;
- event bus string-based;
- callback circolari;
- polling delle modifiche;
- refresh globale ogni frame;
- serializzazione interna per comunicare tra moduli dello stesso processo;
- retry automatici per operazioni locali;
- readiness state tra oggetti già nello stesso processo;
- più fonti autorevoli dello stesso dato.

La UI deve eliminare la necessità di sincronizzare, non generare un nuovo sistema di sincronizzazione.

---

# 3. Una sola autorità authoring

Deve esistere una sola autorità persistente del progetto:

```text
ProjectDocument
```

oppure il nome equivalente che emergerà dal repository reale.

Non creare contemporaneamente:

```text
ProjectDocument
UiProjectModel
InspectorProjectModel
HierarchyProjectModel
RuntimeProjectCopy
```

I pannelli possono mantenere soltanto stato puramente visuale o transitorio:

- filtro di ricerca;
- testo non ancora confermato;
- righe espanse;
- tab attivo;
- dimensioni splitter;
- popup aperti;
- selezione visuale;
- pan e zoom della camera editor;
- valori temporanei durante un drag.

Non devono possedere copie autorevoli di:

- scene;
- entità;
- object type;
- componenti;
- asset;
- Logic Board;
- variabili di progetto;
- configurazione runtime.

La UI legge tramite query e modifica tramite comandi.

---

# 4. Distinguere EditorCommand ed EditorIntent

Usare due percorsi semplici e distinti.

## EditorCommand

Modifica il progetto authoring e può entrare nell’undo stack.

Esempi:

```text
RenameEntityCommand
SetEntityPositionCommand
CreateSceneCommand
DeleteSceneCommand
CreateEntityCommand
DeleteEntityCommand
SetSceneBackgroundCommand
```

Ogni command deve:

1. validare;
2. applicare la mutazione;
3. riportare errore leggibile in caso di fallimento;
4. dichiarare le invalidazioni prodotte;
5. marcare dirty il progetto;
6. fornire undo quando previsto.

## EditorIntent

Modifica soltanto lo stato del workspace/editor.

Esempi:

```text
SelectEntityIntent
SelectSceneIntent
SetViewportZoomIntent
PanViewportIntent
TogglePanelIntent
SetHierarchyFilterIntent
ResizePanelIntent
```

Gli intent non devono essere forzati dentro l’undo authoring.

Il percorso deve restare leggibile:

```text
RmlUi callback
├── execute(command)   per modifiche al progetto
└── apply(intent)      per stato dell’editor
```

Non creare un framework astratto più complesso di questo.

---

# 5. Un solo coordinator

Deve esistere al massimo un coordinator centrale, indicativamente:

```cpp
class EditorCoordinator;
```

Responsabilità consentite:

- possedere o riferire il `ProjectDocument`;
- eseguire command;
- applicare intent;
- mantenere `SelectionState`;
- mantenere `EditorUiState`;
- produrre invalidazioni esplicite;
- coordinare Edit/Play/Stop;
- fornire query in sola lettura ai pannelli;
- registrare errori e messaggi nella console.

Responsabilità vietate:

- disegnare widget;
- contenere codice RML/RCSS;
- possedere il renderer;
- diventare un service locator;
- incorporare filesystem, persistence, import pipeline e runtime in una sola classe;
- duplicare `SceneManager`;
- diventare il nuovo monolite dell’applicazione.

La comunicazione tra pannelli deve passare dal coordinator.

Esempio corretto:

```text
Hierarchy click
→ EditorCoordinator::apply(SelectEntityIntent)
→ SelectionState aggiornata
→ invalidate Hierarchy | Inspector | Viewport
```

Esempio vietato:

```text
Hierarchy
→ callback Inspector
→ callback Viewport
→ event bus
→ runtime sync
→ selection service
```

---

# 6. Invalidazione esplicita e piccola

Usare un sistema di invalidazione tipizzato e limitato.

Esempio:

```cpp
enum class EditorInvalidation : uint32_t {
    None       = 0,
    Hierarchy  = 1u << 0,
    Inspector  = 1u << 1,
    Viewport   = 1u << 2,
    Assets     = 1u << 3,
    Console    = 1u << 4,
    Toolbar    = 1u << 5,
    Project    = 1u << 6,
};
```

Ogni operazione restituisce un risultato esplicito:

```cpp
struct EditorOperationResult {
    bool ok = false;
    EditorInvalidation invalidation = EditorInvalidation::None;
    std::string error;
};
```

Esempio:

```cpp
const auto result = coordinator.execute(
    SetEntityPositionCommand{entityId, newPosition}
);

if (!result.ok) {
    console.showError(result.error);
    return;
}

ui.applyInvalidation(result.invalidation);
```

Non usare eventi generici come:

```text
"scene.changed"
"project.updated"
"entity.modified"
"selection.changed"
```

Non introdurre una gerarchia di invalidazioni dinamiche.

I flag devono restare pochi e comprensibili.

---

# 7. Replace / Select / Patch

Il rapporto tra authoring core e runtime core deve essere ridotto a tre operazioni concettuali.

## Replace

Usato soltanto per operazioni strutturali ampie:

- apertura progetto;
- recovery;
- import completo;
- sostituzione del documento;
- ripristino dopo operazioni distruttive;
- inizializzazione di una PlaySession.

```cpp
runtimeProjection.replaceFrom(document);
```

## Select

Usato per:

- cambio scena attiva;
- selezione entità;
- cambio layer attivo;
- aggiornamento focus editoriale.

```cpp
runtimeProjection.selectScene(sceneId);
```

## Patch

Usato per mutazioni locali:

```cpp
runtimeProjection.applyEntityPatch(entityId, patch);
runtimeProjection.applyScenePatch(sceneId, patch);
```

Non creare altre categorie generiche.

Non usare `Replace` quando basta `Select`.

Non usare `Replace` quando basta `Patch`.

Non serializzare il progetto per applicare queste operazioni nello stesso processo.

---

# 8. Play e Stop

Il documento authoring deve restare separato dalla sessione runtime.

## Play

```text
ProjectDocument
→ crea PlaySession
→ runtime mutabile
```

Durante Play:

- il documento authoring resta immutato;
- il runtime può modificare liberamente entità e stato gameplay;
- la UI può leggere debug state tramite query;
- le modifiche gameplay non devono essere continuamente riportate nel documento.

## Stop

```text
distruggi PlaySession
→ ritorna al ProjectDocument già esistente
```

Stop non deve:

- ricaricare il progetto da JSON;
- ricostruire il documento;
- eseguire scene sync;
- attendere readiness;
- ripristinare lo stato tramite bridge.

Il documento authoring non deve essere stato modificato dalla simulazione.

---

# 9. RmlUi deve restare solo presentazione

RmlUi deve gestire:

- layout;
- styling;
- controlli;
- input UI;
- focus;
- popup;
- pannelli;
- tab;
- liste;
- campi;
- rappresentazione visuale.

RmlUi non deve:

- possedere il progetto;
- conoscere `SceneManager`;
- modificare direttamente entità runtime;
- chiamare direttamente il renderer;
- creare o distruggere scene;
- applicare mutazioni persistenti senza command;
- decidere la semantica di Play/Stop;
- contenere logica di validazione di dominio.

Esempio corretto:

```cpp
void InspectorController::onPositionCommitted(float x, float y) {
    coordinator_.execute(
        SetEntityPositionCommand{
            coordinator_.selection().primaryEntity(),
            {x, y}
        }
    );
}
```

Esempio vietato:

```cpp
sceneManager.getEntity(id).transform.position = {x, y};
```

---

# 10. Data binding RmlUi

Usare il data binding con moderazione.

Sono ammessi binding in sola lettura per:

- testo;
- lista;
- stato selected;
- stato enabled;
- stato visible;
- badge;
- contenuto console;
- etichette.

Evitare binding bidirezionali diretti sul documento.

Vietato:

```text
NumberField.value <=> ProjectDocument.entity.transform.x
```

Preferire:

```text
utente modifica NumberField
→ callback
→ command
→ ProjectDocument
→ invalidazione Inspector | Viewport
→ refresh mirato
```

Il percorso di modifica deve essere visibile nel codice e facilmente tracciabile.

---

# 11. Aggiornamento della UI

Non ricostruire tutti i documenti RML ogni frame.

Ogni pannello deve avere API locali come:

```cpp
class HierarchyPanel {
public:
    void refresh(const EditorReadModel& model);
    void updateSelection(const SelectionState& selection);
};

class InspectorPanel {
public:
    void showSelection(const EditorReadModel& model);
};

class ConsolePanel {
public:
    void append(const ConsoleMessage& message);
};
```

Oppure una forma equivalente più semplice.

Gli aggiornamenti devono avvenire solo in seguito a invalidazioni.

Il normale frame deve assomigliare a:

```cpp
void NativeEditorApp::frame() {
    pollPlatformEvents();
    routeInput();

    coordinator_.processPendingOperations();
    editorUi_.applyInvalidations(coordinator_.consumeInvalidations());

    viewport_.update();
    rmlContext_->Update();

    beginFrame();
    viewport_.render();
    rmlContext_->Render();
    endFrame();
}
```

Durante il normale frame non devono avvenire:

- serializzazione del progetto;
- fingerprint;
- scansione completa non necessaria;
- replace del progetto;
- rebuild dell’intera UI;
- polling dello stato authoring;
- ricreazione di documenti RML.

---

# 12. Cambio scena

Il cambio scena deve essere immediato e deterministico.

```cpp
coordinator.apply(SelectSceneIntent{sceneId});
```

Internamente:

```text
valida sceneId
→ aggiorna activeSceneId authoring/editoriale
→ SceneManager seleziona la scena
→ ripristina EditorSceneViewState
→ invalida Hierarchy | Inspector | Viewport | Toolbar
→ prossimo frame
```

Il cambio scena non deve:

- ricaricare il progetto;
- ricreare la finestra;
- ricreare il context RmlUi;
- ricaricare asset invariati;
- serializzare il documento;
- eseguire retry;
- attendere flag di readiness;
- passare dal lifecycle gameplay;
- applicare fade.

La navigazione editoriale e la transizione gameplay devono restare separate.

---

# 13. Viewport

Il viewport deve leggere direttamente:

```text
active scene
+ presentation state
→ SceneFrameSnapshot
→ renderer
```

RmlUi conosce soltanto il rettangolo del viewport e il relativo stato di interazione.

```cpp
struct ViewportRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};
```

Nessun pannello deve chiamare direttamente il renderer.

Nessuna scena deve essere “montata” nella UI.

Il canvas/viewport deve essere persistente per tutta la sessione editor.

Cambio scena:

```text
activeSceneId cambia
→ nuovo SceneFrameSnapshot
→ frame successivo
```

Non:

```text
smonta viewport
→ ricrea renderer
→ ricarica asset
→ ricrea camera
```

---

# 14. Stato UI minimo

Definire uno stato UI separato, piccolo e non persistente nel progetto.

Esempio:

```cpp
struct EditorUiState {
    float leftPanelWidth = 280.0f;
    float rightPanelWidth = 320.0f;
    float consoleHeight = 220.0f;

    std::string hierarchyFilter;
    std::string assetFilter;

    std::unordered_set<StableId> expandedHierarchyItems;

    bool transformSectionExpanded = true;
    bool spriteSectionExpanded = true;
    bool collisionSectionExpanded = false;
};
```

Definire separatamente la vista editor per scena:

```cpp
struct EditorSceneViewState {
    Vec2 pan{};
    float zoom = 1.0f;
};
```

Memorizzata per `SceneId`.

Non salvare pan e zoom editor dentro la camera gameplay.

---

# 15. Audit iniziale obbligatorio

Prima di modificare il codice:

1. Ispeziona l’intero repository.
2. Identifica:
   - target nativi esistenti;
   - main loop;
   - ownership della finestra;
   - ownership del contesto grafico;
   - uso di Raylib e rlgl;
   - `BeginDrawing` / `EndDrawing`;
   - `FrameCoordinator`;
   - `SceneFrameSnapshot`;
   - `SceneManager`;
   - `RuntimeEntityGateway`;
   - lifecycle Play/Stop;
   - input;
   - resize;
   - DPI;
   - filesystem;
   - CMake;
   - build script Windows;
   - dipendenze e convenzioni third-party.
3. Determina quali moduli possono essere riutilizzati senza refactor.
4. Determina quali moduli sono attualmente accoppiati a WebView/WASM.
5. Individua il minimo confine necessario tra:
   - platform host;
   - UI;
   - authoring;
   - runtime;
   - presentation.
6. Verifica la licenza di RmlUi e delle dipendenze abilitate.
7. Verifica il backend grafico RmlUi più adatto all’attuale renderer.
8. Verifica se il viewport può:
   - disegnare direttamente nella finestra;
   - disegnare in una `RenderTexture`;
   - rispettare un rettangolo e uno scissor;
   - convivere con lo stato grafico RmlUi.

Prima del codice creare:

```text
docs/RMLUI_NATIVE_EDITOR_PLAN.md
```

Il documento deve contenere:

- struttura reale trovata;
- dipendenze coinvolte;
- target proposto;
- file da modificare;
- strategia renderer;
- strategia input;
- strategia di ownership;
- rischi;
- rollback;
- criteri di accettazione;
- elementi deliberatamente fuori scope.

Dopo il report procedi senza attendere approvazione, salvo blocchi architetturali reali.

---

# 16. Strategia di implementazione

Procedere in fasi strette.

## Fase A — target isolato

Creare un executable parallelo.

Non modificare il comportamento dell’editor web.

Non condividere `main()` o lifecycle con WebView.

Il nuovo target deve avviarsi senza Node, npm, WebView2 e WASM.

## Fase B — RmlUi host minimale

Implementare soltanto:

```cpp
class RmlUiHost {
public:
    bool initialize(...);
    void processInput(...);
    void resize(int width, int height, float dpiScale);
    void update();
    void render();
    void shutdown();

private:
    Rml::Context* context_ = nullptr;
};
```

Responsabilità:

- `SystemInterface`;
- `RenderInterface`;
- context;
- font;
- caricamento RML/RCSS;
- input;
- resize;
- update;
- render;
- shutdown.

Nessuna logica progetto dentro `RmlUiHost`.

## Fase C — shell grafica

Creare:

```text
Menu bar
Toolbar
Hierarchy
Assets
Viewport
Inspector
Console
```

Layout fisso con splitter.

Non creare docking universale.

## Fase D — coordinator e demo model

Creare il minimo:

```text
EditorCoordinator
SelectionState
EditorUiState
EditorCommand
EditorIntent
EditorInvalidation
```

Non costruire subito undo completo.

Implementare pochi casi reali:

- selezione entità;
- cambio scena;
- modifica posizione;
- rinomina;
- messaggio console.

## Fase E — viewport reale

Mostrare una scena ArtCade reale.

Obiettivo minimo:

- background;
- scene bounds;
- griglia;
- almeno un’entità;
- resize;
- pan;
- zoom.

Non implementare ancora:

- tile painting;
- gizmo completo;
- multi-selection;
- camera preview;
- Logic Board completo;
- Play/Stop completo.

## Fase F — verifica architetturale

Dimostrare che una modifica Position X segue esattamente:

```text
RmlUi NumberField
→ InspectorController
→ SetEntityPositionCommand
→ ProjectDocument
→ EditorInvalidation::Inspector | Viewport
→ refresh Inspector
→ frame successivo
```

Nessun altro passaggio.

---

# 17. Integrazione RmlUi

Usare il sistema di dipendenze già adottato dal repository.

In assenza di una convenzione chiara:

- usare `FetchContent`;
- fissare tag o commit;
- non puntare a branch mobile;
- non richiedere installazioni globali;
- non scaricare dipendenze a runtime.

Includere:

```text
THIRD_PARTY_NOTICES.md
licenses/RmlUi.txt
```

e le licenze delle dipendenze effettivamente abilitate.

Non modificare la licenza di ArtCade.

---

# 18. Rendering RmlUi + Raylib

Decisione definitiva post bug font: usare `RenderInterface_GL3` ufficiale di
RmlUi sopra il context OpenGL gia' creato da Raylib. Non implementare un
renderer RmlUi proprietario basato su `rlgl`, salvo un wrapper estremamente
sottile attorno al backend ufficiale.

Bug reale osservato:

- il renderer custom `rlgl` disegnava correttamente i solid quad;
- i glyph quad usavano lo stesso percorso geometrico ma diventavano invisibili
  o apparivano come blocchi pieni;
- la differenza critica era la pipeline texture/shader/font atlas, non layout,
  winding o coordinate dei quad.

Il renderer custom duplicava responsabilita' gia' risolte dal backend ufficiale:

- upload delle texture font;
- shader RmlUi;
- texture handle;
- geometrie compilate;
- blending;
- scissor;
- stencil e clip mask;
- gestione dello stato OpenGL;
- rilascio di texture e geometrie.

Confine da mantenere:

```text
ArtCade
├── context ownership
├── frame order
├── resize e DPI
├── input routing
├── resource paths
├── inizializzazione
└── shutdown

RmlUi RenderInterface_GL3
├── font atlas upload/sampling
├── shader e texture binding
├── compiled geometry
├── blending, scissor, stencil/clip mask
├── stato OpenGL richiesto da RmlUi
└── rilascio risorse grafiche RmlUi
```

Principio architetturale:

```text
ArtCade personalizza la UI e l'integrazione applicativa,
non reimplementa la pipeline grafica interna di RmlUi.
```

Usare un solo:

- processo;
- thread UI/render;
- context grafico;
- loop principale;
- proprietario della finestra.

Ordine frame raccomandato:

```cpp
pollInput();
updateWindowMetrics();
routeInputToRml();

editorCoordinator.applyPendingOperations();
editorUi.applyInvalidations();

rmlContext->Update();

BeginDrawing();

renderSceneViewport();

// Boundary grafico esplicito: viewport Raylib prima, RmlUi GL3 dopo.
renderRmlUi();

EndDrawing();
```

Regola importante:

```text
viewport Raylib
-> boundary grafico esplicito
-> RmlUi GL3
```

Nessun `BeginDrawing` annidato, nessun secondo context, nessuna seconda finestra
e nessun secondo main loop.

Evitare di copiare progressivamente parti del backend GL3 dentro `rlgl`: quella
strada ricrea esattamente il problema appena eliminato.

Le opzioni sotto restano note storiche, non alternative equivalenti alla
decisione corrente.

## Nota storica 1 - renderer custom `rlgl` deprecato

Non adattare un `Rml::RenderInterface` custom su `rlgl` per il nuovo editor. Il
tentativo precedente ha dimostrato che questa strada rende fragile il rendering
dei font.

Il renderer custom richiederebbe di gestire esplicitamente:

- flush rlgl;
- scissor;
- blending;
- texture binding;
- shader;
- viewport;
- projection;
- stato OpenGL;
- ripristino dello stato.

Nessun `BeginDrawing` annidato.

## Decisione corrente - backend RmlUi OpenGL adattato

Compilare e usare il backend GL3 ufficiale di RmlUi nel loop ArtCade, con il
GL loader coerente con Raylib. L'integrazione deve restare sottile: ArtCade
decide quando aggiornare/renderizzare la UI, il backend decide come disegnare
la pipeline RmlUi.

## Nota storica 2 - viewport in render texture

Solo se necessario:

```text
ArtCade Renderer
→ RenderTexture
→ texture mostrata nel rettangolo viewport
→ RmlUi sopra/intorno
```

Nessuna copia CPU per frame.

Documentare la scelta.

Non riscrivere il renderer salvo la minima separazione necessaria tra:

```text
window ownership
e
scene rendering
```

Checklist regressione font/UI:

- [ ] brand leggibile;
- [ ] menu leggibili;
- [ ] tab leggibili;
- [ ] toolbar leggibile;
- [ ] Hierarchy leggibile;
- [ ] Inspector leggibile;
- [ ] Console leggibile;
- [ ] input text leggibile;
- [ ] resize senza glyph corrotti;
- [ ] DPI 100%, 125%, 150%, 200%;
- [ ] viewport ancora corretto dopo il rendering UI;
- [ ] UI ancora corretta dopo rendering viewport;
- [ ] nessun errore OpenGL in debug;
- [ ] Inter Regular leggibile;
- [ ] Inter Medium leggibile;
- [ ] Inter SemiBold leggibile;
- [ ] Inter Bold leggibile.

---

# 19. Input routing

Definire una sola pipeline:

```text
evento piattaforma
→ RmlUi
→ se consumato: stop
→ altrimenti editor viewport
→ altrimenti PlaySession
```

Gestire:

- mouse move;
- left/right/middle;
- wheel;
- key down/up;
- testo Unicode;
- Ctrl/Shift/Alt;
- clipboard;
- focus;
- resize;
- perdita focus;
- DPI.

Il viewport riceve input soltanto se:

- il cursore è dentro il viewport;
- nessun popup RmlUi intercetta;
- nessun campo testo ha focus;
- la UI non ha consumato l’evento.

Non leggere lo stesso input indipendentemente in più subsystem.

---

# 20. Layout dello spike

Realizzare:

```text
┌──────────────────────────────────────────────────────────┐
│ Menu                                                     │
├──────────────────────────────────────────────────────────┤
│ Toolbar                                                  │
├───────────────┬──────────────────────────┬───────────────┤
│ Hierarchy     │                          │ Inspector     │
│               │        Viewport          │               │
│ Assets        │                          │               │
├───────────────┴──────────────────────────┴───────────────┤
│ Console / Output / Problems                              │
└──────────────────────────────────────────────────────────┘
```

Tre splitter:

- sinistra/centro;
- centro/destra;
- workspace/console.

Nessun docking libero.

Persistenza layout semplice:

```cpp
struct EditorLayoutSettings {
    float leftWidth;
    float rightWidth;
    float consoleHeight;
};
```

Non serializzare un docking tree.

---

# 21. Design visivo

La UI deve dimostrare che RmlUi può superare il look troppo flat di Dear ImGui.

Direzione:

- dark premium;
- antracite;
- pannelli su livelli leggermente differenti;
- bordi sottili;
- radius piccoli;
- ombre leggere;
- contrasto controllato;
- accent blu freddo;
- font compatto e leggibile;
- toolbar dense;
- input professionali;
- hover/focus visibili;
- niente glow;
- niente neon;
- niente card da dashboard SaaS;
- niente look ImGui;
- niente gradienti vistosi.

Usare RCSS centralizzato:

```text
theme.rcss
layout.rcss
controls.rcss
panels.rcss
```

Evitare stile inline.

---

# 22. Primitive UI minime

Creare soltanto ciò che serve:

```text
EditorButton
EditorIconButton
EditorTab
EditorPanel
EditorSection
EditorPropertyRow
EditorNumberField
EditorCheckbox
EditorSelect
EditorTreeRow
EditorSplitter
EditorStatusBadge
```

Preferire:

```text
RML
+ RCSS
+ controller C++ piccolo
```

Creare custom element RmlUi soltanto se il comportamento non è esprimibile pulitamente.

Non creare una classe C++ per ogni widget statico.

---

# 23. Struttura indicativa

Adattare ai pattern reali del repository.

```text
editor-native/
├── include/
│   ├── native_editor_app.h
│   ├── editor_coordinator.h
│   ├── editor_command.h
│   ├── editor_intent.h
│   ├── editor_invalidation.h
│   ├── editor_ui_state.h
│   ├── rmlui_host.h
│   ├── rmlui_render_interface.h
│   ├── rmlui_system_interface.h
│   ├── editor_ui.h
│   ├── hierarchy_panel.h
│   ├── inspector_panel.h
│   ├── console_panel.h
│   └── editor_viewport.h
│
├── src/
│   ├── main.cpp
│   ├── native_editor_app.cpp
│   ├── editor_coordinator.cpp
│   ├── rmlui_host.cpp
│   ├── rmlui_render_interface.cpp
│   ├── rmlui_system_interface.cpp
│   ├── editor_ui.cpp
│   ├── hierarchy_panel.cpp
│   ├── inspector_panel.cpp
│   ├── console_panel.cpp
│   └── editor_viewport.cpp
│
└── resources/
    ├── ui/
    │   ├── editor_shell.rml
    │   ├── hierarchy.rml
    │   ├── inspector.rml
    │   ├── console.rml
    │   ├── theme.rcss
    │   ├── layout.rcss
    │   ├── controls.rcss
    │   └── panels.rcss
    ├── fonts/
    └── icons/
```

Ridurre il numero di file se il repository suggerisce una struttura più semplice.

Non creare astrazioni vuote.

---

# 24. Test obbligatori

Aggiungere test per:

1. Un command modifica una sola autorità.
2. Un command fallito non modifica lo stato.
3. Un command fallito non produce invalidazioni.
4. `SetEntityPositionCommand` invalida soltanto Inspector e Viewport.
5. Una selezione non esegue Replace.
6. Un cambio scena non serializza il progetto.
7. Un cambio scena non ricrea RmlUi.
8. Nessun pannello si aggiorna senza invalidazione.
9. La UI non modifica direttamente `SceneManager`.
10. Il viewport non possiede il progetto.
11. Play non modifica `ProjectDocument`.
12. Stop non richiede reload JSON.
13. Il parsing invalido di un NumberField non modifica il documento.
14. Lo shutdown è idempotente.
15. Resize aggiorna context RmlUi e viewport.
16. Input catturato da un campo testo non raggiunge il viewport.
17. Lo splitter applica clamp min/max.
18. Una modifica Position X attraversa soltanto:

```text
UI callback
→ command
→ document
→ invalidation
```

Usare fake/spy per dimostrare l’assenza di chiamate aggiuntive.

---

# 25. Criteri di fallimento architetturale

Considerare lo spike fallito se introduce:

- un `NativeRuntimeSyncService`;
- un `UiSynchronizer`;
- un `ProjectReplicationService`;
- un event bus generale per la UI;
- serializzazione JSON tra UI e core;
- polling dello stato authoring;
- refresh globale per frame;
- data binding bidirezionale sul documento;
- più modelli autorevoli;
- retry automatici per modifiche locali;
- readiness flag tra oggetti nello stesso processo;
- callback UI che mutano direttamente il runtime;
- un `NativeEditorApp` monolitico;
- un refactor massivo del runtime non necessario;
- docking universale nello spike;
- ricostruzione del progetto durante cambio scena;
- ricostruzione UI durante cambio scena.

---

# 26. Build

Aggiungere un flusso esplicito:

```text
build_native_editor.bat
run_native_editor.bat
```

oppure integrare nel sistema esistente mantenendo target separato.

Il nuovo build non deve richiedere:

- npm;
- Node;
- WebView2;
- WASM;
- browser runtime.

Deve:

- configurare CMake;
- compilare;
- copiare RML/RCSS/font/icon;
- riportare errori chiari;
- funzionare da clean checkout;
- non sovrascrivere output dell’editor web.

---

# 27. Criteri di accettazione

Lo spike è accettato soltanto se:

1. Compila da repository pulito.
2. L’editor web continua a compilare.
3. Si apre una finestra ArtCade nativa.
4. RmlUi renderizza la shell.
5. Il layout è visivamente professionale.
6. I tre splitter funzionano.
7. Input testo, mouse e wheel funzionano.
8. Il viewport mostra una scena ArtCade reale.
9. Resize non rompe il layout.
10. DPI 100% e 150% risultano leggibili.
11. Non esiste WebView2 nel nuovo target.
12. Non esiste WASM nel nuovo target.
13. Non esiste JSON bridge nel normale editing.
14. Non esiste un servizio di sincronizzazione.
15. Cambio scena non ricarica il progetto.
16. Modifica Inspector non esegue full replace.
17. La UI usa command/intent.
18. Il percorso Position X è lineare.
19. Shutdown non produce errori o leak evidenti.
20. Le modifiche sono isolate e reversibili.

---

# 28. Deliverable

Creare:

```text
docs/RMLUI_NATIVE_EDITOR_PLAN.md
docs/RMLUI_NATIVE_EDITOR_REPORT.md
```

Nel report finale includere:

- commit prodotti;
- file modificati;
- architettura effettiva;
- diagramma del flusso di modifica;
- backend RmlUi usato;
- integrazione Raylib/OpenGL;
- input routing;
- ownership;
- performance osservata;
- rischi;
- debito introdotto;
- elementi non implementati;
- differenze rispetto al piano;
- difficoltà di migrazione per ogni pannello:
  - facile;
  - media;
  - difficile.

Diagramma richiesto:

```text
RmlUi Event
    ↓
Panel Controller
    ↓
EditorCoordinator
    ↓
EditorCommand / EditorIntent
    ↓
ProjectDocument / EditorUiState
    ↓
EditorInvalidation
    ├── Hierarchy refresh
    ├── Inspector refresh
    └── Viewport next frame
```

Se nel diagramma compaiono:

```text
sync
poll
retry
readiness
replicate
fingerprint
reconcile
```

spiegare perché sono necessari.

Nel normale percorso authoring non dovrebbero comparire.

Concludere con:

```text
GO
GO WITH CONDITIONS
NO-GO
```

motivato da evidenze tecniche.

---

# 29. Strategia commit

Usare commit piccoli:

```text
1. Document RmlUi native editor architecture
2. Add pinned RmlUi dependency and native target
3. Add RmlUi platform and rendering host
4. Add editor coordinator, commands, intents and invalidation
5. Add native editor shell and ArtCade theme
6. Add split layout and basic panels
7. Integrate real ArtCade viewport
8. Add architectural tests and final report
```

Non concentrare tutto in un unico commit.

Dopo ogni fase:

- compilare;
- eseguire test;
- avviare smoke test;
- correggere prima di proseguire.

---

# 30. Regola finale

La qualità del lavoro non viene misurata dal numero di pannelli portati.

Viene misurata da quanto è semplice spiegare una modifica concreta.

Il caso di riferimento è:

```text
NumberField Position X
→ SetEntityPositionCommand
→ ProjectDocument
→ Inspector | Viewport invalidation
→ frame successivo
```

Questo percorso deve:

- essere sincrono;
- avere una sola autorità;
- non serializzare;
- non fare polling;
- non fare retry;
- non attraversare observer generici;
- non ricreare la scena;
- non ricaricare il progetto;
- non dipendere dal lifecycle della UI.

La semplicità architetturale ha precedenza su:

- generalizzazione;
- automazione;
- data binding avanzato;
- estensibilità teorica;
- framework interni;
- riuso prematuro;
- quantità di codice migrato.

Prima di ogni nuova astrazione chiediti:

```text
Questa classe elimina complessità reale
oppure nasconde un nuovo percorso di sincronizzazione?
```

In caso di dubbio, scegliere la soluzione più diretta, tipizzata e locale.

---

# 31. Convenzioni obbligatorie per nomi, struttura e correzioni emerse durante il refactor

La qualità dei nomi di file, directory, classi e funzioni è parte dei criteri di accettazione.

Non considerare il naming un dettaglio cosmetico.

Il nuovo editor deve essere facilmente navigabile senza dover aprire ogni file per capirne la responsabilità.

---

## Principi di naming

Ogni nome deve essere:

- breve;
- preciso;
- professionale;
- coerente con il dominio;
- comprensibile senza contesto implicito;
- corrispondente a una sola responsabilità.

Evitare nomi:

- vaghi;
- generici;
- ridondanti;
- eccessivamente lunghi;
- basati sulla storia del refactor;
- contenenti abbreviazioni non standard;
- contenenti suffissi inutili;
- che descrivono più responsabilità contemporaneamente.

---

## Nomi vietati o fortemente sconsigliati

Non creare file o classi con nomi come:

```text
utils
helpers
common
misc
shared
base_manager
global_manager
master_controller
editor_system
native_system
runtime_ui_manager
ui_bridge_service
state_sync_service
editor_state_manager
general_controller
data_handler
model_handler
application_context
service_registry
```

Questi nomi tendono a nascondere responsabilità indefinite.

Non usare suffissi come:

```text
Impl
New
V2
Final
Refactored
LegacyReplacement
NativeVersion
RmlUiVersion
```

salvo necessità tecnica reale e documentata.

Il nome non deve raccontare che il codice è nato durante una migrazione.

Deve descrivere ciò che il codice è.

---

## Lunghezza dei nomi

Preferire:

```text
editor_app.cpp
editor_coordinator.cpp
project_document.cpp
selection_state.cpp
command_stack.cpp
rml_host.cpp
rml_renderer.cpp
rml_system.cpp
editor_ui.cpp
hierarchy_panel.cpp
inspector_panel.cpp
console_panel.cpp
scene_view.cpp
```

Evitare:

```text
native_editor_application_runtime_controller.cpp
rmlui_editor_render_interface_adapter_impl.cpp
artcade_editor_project_document_state_manager.cpp
```

Se un nome necessita di quattro o cinque concetti per essere spiegato, probabilmente la classe possiede troppe responsabilità.

---

## Nomi RmlUi

Usare il prefisso `rml_` soltanto per elementi che dipendono realmente da RmlUi.

Esempi corretti:

```text
rml_host.cpp
rml_renderer.cpp
rml_system.cpp
rml_input.cpp
```

Non usare `rml_` per classi di dominio o pannelli che potrebbero teoricamente sopravvivere a un cambio di toolkit.

Preferire:

```text
inspector_panel.cpp
hierarchy_panel.cpp
console_panel.cpp
```

non:

```text
rmlui_inspector_panel.cpp
rmlui_hierarchy_controller.cpp
```

Il toolkit non deve contaminare i nomi del dominio editoriale.

---

## Corrispondenza file-classe

Quando possibile:

```text
editor_coordinator.h
editor_coordinator.cpp
class EditorCoordinator
```

```text
scene_view.h
scene_view.cpp
class SceneView
```

Evitare che un file contenga più classi principali non correlate.

Piccoli tipi locali e helper strettamente collegati possono restare nello stesso file.

Non creare un file separato per ogni struct di tre righe se questo frammenta inutilmente il codice.

---

## Directory

Usare directory basate su responsabilità stabili, non su dettagli della migrazione.

Struttura indicativa:

```text
editor-native/
├── app/
├── ui/
├── commands/
├── model/
├── platform/
├── view/
└── resources/
```

oppure una variante ancora più piccola se il repository lo consente.

Evitare strutture profonde come:

```text
editor-native/
└── infrastructure/
    └── integration/
        └── adapters/
            └── rmlui/
                └── implementations/
```

Non introdurre più di due o tre livelli senza una necessità concreta.

La directory deve semplificare la navigazione, non rappresentare ogni concetto teorico dell’architettura.

---

## Funzioni

Preferire verbi espliciti:

```cpp
openProject()
saveProject()
selectScene()
selectEntity()
setEntityPosition()
refreshInspector()
refreshHierarchy()
applyInvalidations()
renderScene()
```

Evitare verbi vaghi:

```cpp
handle()
process()
manage()
sync()
updateEverything()
doWork()
executeAction()
```

`update()` e `render()` sono accettabili nei lifecycle chiaramente definiti.

`sync` non deve essere usato per aggiornamenti locali tra oggetti nello stesso processo.

---

## Correzione di nomi esistenti

Se durante il refactor vengono incontrati nomi nuovi o preesistenti:

- chiaramente ambigui;
- non più coerenti con la responsabilità;
- fuorvianti dopo la nuova architettura;
- causa di duplicazioni o confusione;

rinominarli durante il refactor quando il cambiamento è locale, sicuro e coperto da build o test.

Non avviare una rinomina globale dell’intero repository solo per uniformità estetica.

La rinomina deve avere una motivazione architetturale concreta.

---

# 32. Gestione delle incongruenze incontrate durante il lavoro

Le specifiche di questo prompt sono una direzione architetturale, ma il repository reale è la fonte della verità sui vincoli tecnici esistenti.

Se durante l’audit o l’implementazione emergono:

- responsabilità duplicate;
- ownership incoerente;
- API incompatibili;
- lifecycle contraddittori;
- dipendenze circolari;
- assunzioni errate nelle specifiche;
- tipi mancanti;
- accoppiamenti che impediscono il percorso lineare;
- bug dimostrati;
- naming che nasconde responsabilità differenti;

non aggirare il problema con:

- adapter temporanei permanenti;
- nuovi servizi di sincronizzazione;
- copie di stato;
- fallback silenziosi;
- workaround tramite polling;
- duplicazione delle API;
- classi `Legacy`, `Compat` o `Bridge` non strettamente temporanee.

Correggere l’incongruenza nel punto più vicino alla sua origine, mantenendo il cambiamento minimo necessario.

---

## Regola per decidere se correggere subito

Correggere durante il refactor quando il problema:

1. impedisce il nuovo target;
2. viola una sola autorità;
3. richiede stato duplicato;
4. costringe a introdurre un sync;
5. rende ambiguo l’ownership;
6. provoca un bug riproducibile;
7. rende impossibile testare il percorso lineare;
8. contraddice direttamente Replace / Select / Patch;
9. costringe la UI a conoscere il runtime;
10. impedisce il clean shutdown o il frame deterministico.

Non correggere immediatamente quando il problema:

- è puramente estetico;
- appartiene a una feature fuori scope;
- richiede una riscrittura estesa non necessaria allo spike;
- non influenza il nuovo editor;
- riguarda codice deprecato che non viene attraversato;
- è soltanto una preferenza stilistica.

In questi casi documentarlo nel report finale.

---

## Deviazioni dalle specifiche

Se una specifica risulta tecnicamente sbagliata o incompatibile con il repository:

1. non seguirla ciecamente;
2. verificare il codice e i test;
3. scegliere la soluzione più semplice coerente con gli obiettivi;
4. documentare la deviazione;
5. spiegare perché riduce il rischio o la complessità;
6. aggiungere test che proteggano il nuovo comportamento.

Nel report usare una sezione:

```text
Deviations from the initial plan
```

Per ogni deviazione indicare:

```text
Specifica iniziale:
Evidenza trovata:
Decisione:
Motivazione:
Impatto:
Test aggiunti:
```

---

## Divieto di espansione incontrollata

La possibilità di correggere incongruenze non autorizza un refactor generale del repository.

Prima di estendere il perimetro, verificare:

```text
Questa modifica è necessaria per eliminare complessità reale
oppure è soltanto un miglioramento collaterale?
```

Se è collaterale:

- non implementarla;
- annotarla nel report;
- continuare con lo spike.

Se è necessaria:

- applicare il cambiamento minimo;
- aggiungere test;
- isolarlo in un commit dedicato;
- spiegare il motivo nel commit e nel report.

---

# 33. Revisione finale del naming

Prima di considerare completata ogni fase:

1. elencare i nuovi file;
2. verificare che ogni nome descriva una responsabilità;
3. cercare parole vaghe come:
   - manager;
   - service;
   - helper;
   - utils;
   - common;
   - bridge;
   - sync;
   - handler;
4. giustificare ogni occorrenza rimasta;
5. accorciare nomi ridondanti;
6. verificare la corrispondenza file-classe;
7. verificare che nessun nome dipenda dalla migrazione corrente.

Nel report finale includere:

```text
Naming review
```

con:

- file creati;
- eventuali rinomine;
- nomi generici deliberatamente evitati;
- eccezioni motivate.

---

# 34. Criterio finale

Un nuovo sviluppatore deve poter capire la struttura principale leggendo soltanto:

```text
nomi delle directory
nomi dei file
nomi delle classi pubbliche
```

Senza dover conoscere:

- il vecchio editor React;
- la storia del refactor;
- i problemi precedenti di sync;
- il toolkit UI usato prima.

I nomi devono descrivere l’architettura finale, non il percorso usato per raggiungerla.
