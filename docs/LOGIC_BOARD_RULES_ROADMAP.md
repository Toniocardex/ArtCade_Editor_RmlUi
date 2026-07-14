# Logic Board — Roadmap per l'ampliamento delle regole

**Stato:** in corso

## Stato di avanzamento

| Slice | Stato | Evidenza |
|---|---|---|
| 0. Baseline e contract | Completata | Limiti e validazione core coperti dalla suite nativa. |
| 2A. Catalog foundation e picker | Completata | Registry descriptor-driven, compatibilitÃ  e picker RmlUi. |
| 2B. Personaggio controllabile | Completata | Input edge/held e intent riusati dai controller esistenti. |
| 2C. Collisioni e `EventOther` | Completata | Enter/exit deterministici, filtro per Object Type e `Destroy Self` differito. |
| 2D. Animazione e audio | In corso | Prerequisito ownership 2D.0 completato; prossima slice 2D.1. |
| 2D.0A. Schema e migrazione ownership | Completata | Schema v4 type-owned, override sparse, promozione deterministica, round-trip e idempotenza. |
| 2D.0B. Resolver canonico Edit/Play | Completata | Un solo `resolveSpritePresentation`; Viewport, validazione e `PlaySession` consumano gli stessi valori risolti. |
| 2D.0C. Command, Inspector e Undo/Redo | Completata | Command distinti per authority, Undo esatto, badge ownership e Reset al default. |
| 2D.0D. Invarianti e cleanup legacy | Completata | Validator type-owned, guardie Logic Board, asset delete/Undo v4 e rimozione dei Command instance-owned legacy. |
| 2D.1. Action animazione | Da fare | Sbloccata solo dopo 2D.0D. |
| 2D.2. Action audio | Da fare | Successiva alle Action animazione. |
| 2E. Variabili | Da fare | Bloccata dalla decisione di ownership della slice. |
| 2F. Tempo e messaggi | Da fare | Dopo contratto dedicato. |

### Aggiornamenti trasversali completati

- Play avviato dalla Logic Board ora crea prima la `PlaySession` e solo dopo passa alla Scene runtime; Stop ripristina la stessa board, tab e ricerca. Una selezione manuale del workspace durante Play annulla il ritorno automatico.
- Il gameplay keyboard focus e la navigazione Play restano responsabilitÃ  di editor/coordinator; `PlaySession` rimane indipendente dalle tab RmlUi e dall'input host.
**Ambito:** editor RmlUi nativo e `vendor/artcade-runtime`; nessun bridge WASM o codice React.
**Precedenza:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` → `ARTCADE_RMLUI_ARCHITECTURE.md` → `ARTCADE_RMLUI_ENGINEERING_GATES.md`.

## 1. Decisione e risultato atteso

Il prossimo investimento è rendere la Logic Board estendibile e poi consegnare un primo set di regole che permetta un personaggio controllabile, interazioni di collisione, animazioni e suoni. Il visual polish non fa parte di questa roadmap.

Il risultato non è una nuova lista nella UI. L'unico catalogo canonico è il registry del modulo `logic-core` nel runtime. La UI lo legge per comporre i picker; `ProjectDocument` salva solo `typeId` stabile e proprietà tipizzate; il compiler produce il programma e le feature richieste; `PlaySession` e il runtime eseguono solo dati materializzati.

```text
descriptor canonico → Command → ProjectDocument → serializer/validator
       ↓                                            ↓
 picker RmlUi ← proiezione/read-only          compiler → PlaySession/runtime
```

Questa scelta elimina la duplicazione oggi presente fra il registry e i menu hard-coded del pannello, senza introdurre manager, target CMake, cache o job system aggiuntivi.

## 2. Contratti trasversali non negoziabili

| Area | Contratto |
|---|---|
| Autorità | `ProjectDocument` possiede `LogicBoardDef`; il registry è metadata statico e il runtime possiede soltanto lo stato Play. |
| Mutazione | Una selezione confermata nel picker emette un Intent e un solo `EditorCommand` atomico. Aprire, cercare, cambiare categoria o chiudere il picker non rende dirty. |
| Persistenza | Persistono esclusivamente ID e proprietà; mai label, categoria, icona, descrizione o Lua generato. |
| Compatibilità | I `typeId` pubblicati sono stabili. Una rinomina richiede alias esplicito o migration testata. |
| Validazione | Il core valida kind, proprietà, componenti richiesti, capability di contesto, asset e riferimenti prima della compilazione. Il runtime non corregge incompatibilità silenziosamente. |
| Play | La board compilata non legge `ProjectDocument`; le Action mutano solo il mondo della `PlaySession`, mai Raylib o l'authoring. |
| Determinismo | Condizioni e Action sono valutate nell'ordine persistito; le condizioni MVP sono in AND. |
| Lifecycle | Trigger, timer e subscription sono posseduti dallo scope della board e disposti su Stop, destroy dell'istanza o replace. |

### Compatibilità da fissare prima della prima nuova regola

La slice 2A non cambia il formato persistito: i board esistenti continuano a contenere `typeId + properties` e conservano lo schema corrente. Prima della 2B va documentata la policy API:

1. le feature di un programma restano la verifica puntuale primaria (`requiredFeatures`);
2. un cambio incompatibile dell'API Lua richiede aumento di `apiVersion` e migration/gestione esplicita delle board precedenti;
3. un type ID rimosso o rinominato richiede alias o migration, mai fallback a un blocco differente.

## 3. Sequenza di implementazione

### 0. Baseline e contract test

**Obiettivo:** congelare il comportamento attuale prima del refactor del catalogo.

- Inventariare i cinque type ID esistenti e aggiungere test di round-trip, compilazione e Undo/Redo per `On Start`, `Key Pressed`, `Set Visible`, `Set Position` e `Is Grounded`.
- Registrare i contratti di input: `Key Pressed` è edge-triggered; le nuove regole distingueranno esplicitamente edge e stato mantenuto.
- Verificare che `LogicProgram.requiredFeatures` sia controllato nel runtime che carica il programma e che un feature mancante impedisca l'avvio in modo diagnostico, non un'esecuzione parziale.
- Definire i limiti misurabili per board, regole, condizioni, action, callback/eventi per frame e subscription; riusare i limiti esistenti quando sono già adeguati.

**Uscita:** test verdi sul comportamento corrente e nota di compatibilità API approvata. Nessuna nuova regola utente.

### 2A. Catalog foundation e picker descriptor-driven

**Obiettivo:** ogni futuro blocco entra nel catalogo senza modificare la struttura del pannello.

**Core/runtime**

- Estendere `LogicBlockDescriptor` con `categoryId`, `description`, proprietà, `requiredComponents`, `requiredContext`, `requiredFeature` e `requiresTick`. I valori sono metadata immutabili del registry, non una seconda copia del documento.
- Introdurre alias tipizzati minimi (`LogicBlockTypeId`, `LogicCategoryId`) e `LogicContextCapability`: `Self`, `EventOther`, `DeltaTime`, `CollisionContact`, `MessagePayload`.
- Rendere `validateBoard` capace di ricevere il contesto authoring necessario per verificare componenti e capability; mantenere i controlli su kind e proprietà nel core. Il compiler usa i descriptor per raccogliere le feature richieste, senza una seconda tabella `if` nella UI.
- Conservare il formato `typeId + properties`. Non salvare metadata o Lua.

**Editor RmlUi**

- Sostituire le righe selezionabili hard-coded con un picker riusabile per Trigger, Condition e Action: categorie, ricerca, descrizione, tastiera, stato disabled e motivo dell'incompatibilità.
- Il picker mantiene soltanto stato visuale locale (aperto, query, categoria, indice focus). I risultati sono una proiezione read-only del registry più del contesto della regola.
- Collegare la conferma ai Command esistenti o a un unico Command di replace del blocco che conservi l'Undo esatto. Non introdurre Command per refresh, filtro o validazione UI.
- Le proprietà con pochi valori restano controlli in linea (`mode-option`); solo la scelta del tipo di blocco passa dal picker.

**Test e DoD**

- Test unitari per default descriptor, type ID duplicati, metadata incoerenti, incompatibilità di componente/capability e feature raccolte.
- Test dei Command per replace/add di ogni kind, no-op, Undo/Redo e board invalida senza mutazione.
- Contract test RmlUi: attach/detach, ricerca, navigazione tastiera, entry disabled, nessun dirty all'apertura e un solo Command alla conferma.
- Build nativa e test core; nessun nuovo comportamento runtime.

**Dipendenza sbloccata:** tutte le ondate successive.

### 2B. Personaggio controllabile

**Obiettivo:** dimostrare una Logic Board utile end-to-end senza esporre velocità fisiche che aggirino il `PlatformerController`.

| Kind | Blocchi |
|---|---|
| Trigger | `input.key_released`, `input.key_held` |
| Condition | `platformer.is_grounded` completato nel runtime |
| Action | `platformer.move_horizontal`, `platformer.jump` |

- `Key Released` è edge-triggered; `While Key Held` viene eseguito una volta per tick solo per le board che lo dichiarano (`requiresTick = true`).
- `Move Horizontal` accetta un asse finito e normalizzato/range-validato; compila in `platformer.requestMove(owner, axis)`. `Jump` compila in `platformer.requestJump(owner)`. Il controller conserva accelerazione, velocità massima, collisione, gravità e decisione grounded.
- I tre blocchi platformer richiedono `Self` e `PlatformerController`; il validator li rifiuta in authoring se il tipo proprietario ne è privo.
- Fissare l'ordine runtime: campionamento input → dispatch Logic Board → consumo degli intent da parte del controller → fisica. Il test usa `dt` esplicito.

**Demo/DoD:** A/D muovono, Space salta soltanto se grounded; rilascio tasto non genera azioni spurie; una board event-only non riceve tick. Coprire validazione, compiler, host, Play/Stop ripetuti e diagnostica con entity, board e rule.

### 2C. Collisioni e contesto `EventOther`

**Obiettivo:** consentire pickup/hazard senza rendere `Other` disponibile fuori da un evento che lo produce.

| Kind | Blocchi |
|---|---|
| Trigger | `collision.enter`, `collision.exit` |
| Condition | `collision.other_is_object_type` |
| Action | `entity.destroy_self` |

- Il runtime produce eventi di transizione deterministici con uno snapshot di contatto e `EventOther`; non passa puntatori live di fisica o UI.
- `Other Is Object Type` richiede `EventOther`. La stessa capability verrà riutilizzata in seguito da `Other Has Tag` e `Destroy Other`.
- La distruzione durante dispatch è differita/sicura: l'event loop termina su uno snapshot, poi applica la mutazione del mondo runtime. Ogni scope della board dell'entità distrutta viene cancellato.

**Demo/DoD:** collisione con un tipo specifico distrugge il pickup; `Other` non è selezionabile per `On Start` o input; enter/exit non duplicano eventi; destroy durante callback, Stop e Replace Project restano sicuri.

### 2D. Animazione e audio

**Obiettivo:** collegare il feedback del gameplay agli asset già authoring, senza incorporare editor di asset nella Logic Board.

#### 2D.0 — Prerequisito: ownership SpriteRenderer/SpriteAnimator

La capability completa e i default appartengono all'Object Type. Una
`SceneInstanceDef` persiste soltanto delta opzionali; campo assente significa
inherit. Il resolver canonico applica default type-owned → override sparse →
validazione → proiezione Edit/Play. I progetti v3 vengono promossi in ordine
canonico di Scene ID e ordine persistito delle istanze; i casi misti conservano
esattamente l'assenza tramite il solo flag migration-only `capabilityEnabled`.

- **2D.0A completata:** schema v4, codec, migrazione pura e idempotente, nessun
  blocco legacy risalvato sulle istanze.
- **2D.0B completata:** `resolveSpritePresentation` è l'unica precedenza usata
  da viewport, validator e materializzazione Play.
- **2D.0C completata:** Command separati per default Object Type e delta istanza,
  Undo/Redo esatto, Inspector con badge `OBJECT TYPE` / `INSTANCE OVERRIDE` e
  azione `Reset to Object Type`.
- **2D.0D completata:** la rimozione Animator è bloccata quando richiesta dalla
  board; i componenti completi v3 esistono solo come input temporaneo del decoder
  e della migrazione; i vecchi Command instance-owned e i relativi verb di
  `ProjectDocument` sono rimossi. Validator, cancellazione asset e Undo/Redo
  operano esclusivamente su default dell'Object Type e override sparsi. Coperti
  i casi negativi di override orfano, velocità non positiva e clip referenziata.

| Kind | Blocchi |
|---|---|
| Action | `animation.play_clip`, `animation.stop`, `animation.set_playback_speed`, `audio.play_sound` |

- Le proprietà persistono `animationAssetId`, `clipId` e `audioAssetId`, mai nomi o path. Le liste del picker sono proiezioni read-only del catalogo asset di `ProjectDocument`.
- Validare asset e clip esistenti, appartenenza dell'asset, tipo dell'asset e componente richiesto all'owner. Materializzare le risoluzioni nel runtime; nessuna query al documento durante Play.
- Definire comportamento esplicito per clip/suono cancellati o non risolvibili: il validator blocca Play con diagnostica; nessun fallback silenzioso.

**Demo/DoD:** salto → Jump + clip + suono; round-trip di tutti gli ID; asset/clip mancanti, replace e Stop non lasciano handle o callback residue.

### 2E. Variabili

**Obiettivo:** introdurre stato gameplay mutabile con ownership e scope espliciti. È una slice autonoma, non un'aggiunta al catalogo precedente.

**Decisioni da approvare prima del codice**

- Oggetto Type: definizione (`VariableId`, tipo, valore iniziale, limiti).
- Entity instance: solo override iniziale consentito dalla policy.
- PlaySession: unico valore mutabile a runtime.
- Logic Board: referenzia solo `VariableId`; il nome è presentazione.

Poi aggiungere in ordine: modello/serializer e migration se necessaria; validator; materializzazione; host runtime; Conditions `compare_number`, `boolean_is`, `string_equals`; Actions `set`, `add`, `subtract`, `toggle`; picker e test. Nessuna modifica runtime torna nell'authoring senza un futuro comando esplicito di Apply Runtime Changes.

**Demo/DoD:** danno a un nemico decrementa Health nella PlaySession; Undo/Redo agisce sulle definizioni authoring, non sui valori runtime; gli scope e i tipi invalidi falliscono con diagnostica.

### 2F. Tempo e messaggi — backlog dopo le variabili

| Kind | Blocchi |
|---|---|
| Trigger | `system.every_tick`, `system.after_delay`, `system.every_interval`, `message.on_message` |
| Action | `message.send_message` |

Questa fase richiede un mini-contract separato per ordine di dispatch, `dt`, payload, scope, limite timer/subscription e cancellazione. Non anticiparla: un tick globale o timer impliciti violerebbero il requisito event-only della roadmap.

## 4. Backlog esplicitamente non incluso nelle prime ondate

Restano fuori fino a una slice dedicata: `Other Has Tag`, `Destroy Other`, `Translate`, `Is Visible`, `Set Position` specializzato, branching OR/gruppi, variabili collection, messaggi con payload complesso e qualsiasi scripting utente. Possono riusare foundation e capability già introdotte, ma non devono entrare come blocchi solo visuali.

## 5. Gate obbligatori per ciascuna ondata

Prima di iniziare una slice, registrare: autorità, Intent/Command, invarianti, Undo/Redo, dirty/revision, schema/migration, failure path, owner/lifetime, comportamento Edit/Play, proiezioni RmlUi e test. La chiusura richiede:

- test unitari di registry, validation, compiler e runtime host;
- test Command/Undo/Redo, round-trip e invalid-data per ogni proprietà nuova;
- test di integrazione PlaySession e runtime materialization;
- contract/lifecycle del pannello e del picker RmlUi;
- test negativi su feature mancante, componenti/context incompatibili, asset/reference mancanti e callback/timer invalidati;
- build nativa e suite core pertinenti verdi.

Una famiglia è considerata completata solo quando percorre integralmente descriptor → authoring → Command → persistenza → validazione → compiler → runtime host → PlaySession → UI → test. Non si avvia la famiglia successiva con TODO o fallback nel percorso precedente.

## 6. Ordine operativo

```text
0 Baseline/compatibilità
        ↓
2A Catalog foundation + picker
        ↓
2B Input + platformer              → prima demo giocabile
        ↓
2C Collisioni + EventOther         → pickup/hazard
        ↓
2D Animazione + audio              → feedback gameplay
        ↓
2E Variabili                        → stato gameplay
        ↓
2F Tempo + messaggi                 → solo dopo contract dedicato
```

La priorità pratica è quindi 2A, non l'aggiunta immediata di nuovi blocchi: protegge l'autorità del registry e riduce il costo di ogni ondata successiva.
