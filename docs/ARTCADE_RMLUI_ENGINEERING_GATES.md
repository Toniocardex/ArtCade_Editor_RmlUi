# ArtCade RmlUi — Engineering Gates

**Ambito:** processo operativo, Definition of Done, test, CI, sicurezza, performance e review  
**Documenti superiori:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`, `ARTCADE_RMLUI_ARCHITECTURE.md`  
**Scopo:** trasformare i principi in verifiche concrete senza imporre infrastruttura non necessaria

---

## 1. Regola di applicabilità

I gate si dividono in:

- **sempre applicabili:** autorità, mutazioni, invarianti, errori, test pertinenti;
- **applicabili alla feature:** persistenza, Play, RmlUi, asset, thread, WASM;
- **condizionali:** archivi, plugin, rete, updater, hot reload e altre superfici non ancora presenti.

Un gate condizionale può essere marcato **N/A** soltanto perché la relativa superficie non esiste. Non può essere ignorato quando la feature viene introdotta.

Non è richiesto implementare infrastruttura preventiva per superare gate N/A.

---

## 2. Severità

### Gate P0

Blocca merge e release.

### Gate P1

Deve essere superato prima della chiusura della slice.

### Gate P2

Può essere differito con debito registrato, owner e condizione di chiusura.

---

## 3. Classificazione preliminare della modifica

Prima del lavoro, classificare la modifica:

- dominio persistente;
- workspace/editor state;
- sola UI RmlUi;
- Scene View;
- runtime/Play;
- persistenza/migrazione;
- asset;
- platform;
- performance/refactor;
- bugfix;
- sicurezza.

La classificazione determina i gate pertinenti. Un cambio RCSS puramente visuale non richiede una migrazione; una modifica al modello persistente sì.

---

## 4. Gate di specifica

Prima di scrivere codice devono essere chiari:

- problema;
- obiettivo;
- non-obiettivi;
- unica autorità dei dati;
- stato persistente, workspace e locale;
- invarianti;
- mutazioni previste;
- comportamento Undo/Redo;
- dirty/revision;
- comportamento Edit e Play;
- persistenza e migrazione;
- failure path;
- owner e lifetime;
- pannelli coinvolti;
- proiezione necessaria;
- test minimi;
- sicurezza pertinente;
- impatto nativo/WASM pertinente.

### Blocco P0

Non iniziare se non è chiaro:

- chi possiede il dato;
- come viene modificato;
- cosa succede in caso di errore;
- se la modifica può perdere dati.

---

## 5. Gate di semplicità

Prima di introdurre una nuova struttura rispondere:

- [ ] esiste un problema concreto e riproducibile?
- [ ] la soluzione attuale non è sufficiente?
- [ ] una funzione o classe esistente non può assorbire la responsabilità senza diventare incoerente?
- [ ] l’astrazione protegge un confine, invariante, lifetime o dipendenza reale?
- [ ] la soluzione non crea una seconda autorità?
- [ ] esiste una variante più semplice con la stessa sicurezza?
- [ ] il costo di manutenzione è inferiore al problema risolto?

### Richiedono giustificazione esplicita

- nuovo manager;
- nuovo target CMake;
- nuova interfaccia;
- nuovo event bus;
- nuova cache;
- job system/nuovo worker;
- stato transitorio;
- Composite Command;
- nuovo ViewModel complesso;
- service o repository aggiuntivo.

### Blocco P0

Fermare se la complessità è introdotta soltanto perché il pattern è considerato “più professionale” o perché proposto dall’AI.

---

## 6. Gate del piano architetturale

Il piano deve indicare, quando pertinenti:

- file e moduli toccati;
- target CMake interessati;
- nuovi tipi;
- Intent;
- Command;
- eventuale Composite Command;
- invarianti;
- change set;
- riconciliazione workspace;
- modifiche a `ProjectDocument`;
- modifiche a `EditorState`/`EditorUiState`;
- ViewModel/proiezioni;
- controller RmlUi;
- listener/sessione documentale;
- cache e invalidazioni;
- Play/runtime;
- serializer/migrazione;
- error path;
- test;
- rollback.

### Domande obbligatorie

1. La feature introduce o duplica un’autorità?
2. Esiste già una logica equivalente?
3. Il modulo scelto è il più semplice corretto?
4. Serve realmente un nuovo target o manager?
5. Cosa accade se l’operazione fallisce a metà?
6. Cosa accade in Undo/Redo?
7. Cosa accade durante Play?
8. Cosa accade su Replace Project?
9. Cosa accade con input invalido?
10. Chi distrugge le nuove risorse?

---

## 7. Gate di slice

Ogni slice deve:

- avere un obiettivo principale;
- mantenere il repository compilabile;
- essere testabile;
- non mescolare refactor indipendenti;
- non introdurre doppie autorità temporanee;
- non disabilitare test;
- non lasciare fallback silenziosi;
- avere rollback comprensibile;
- fermarsi solo per rischi reali P0, non per micro-gate burocratici.

Una slice troppo ampia viene divisa prima dell’implementazione.

---

## 8. Ordine di implementazione raccomandato

Quando pertinente:

1. modello e invarianti;
2. Command e Undo;
3. test core;
4. serializer/migrazione;
5. Coordinator/Intent;
6. riconciliazione e change set;
7. proiezione/ViewModel;
8. controller RmlUi;
9. RML/RCSS;
10. Scene View;
11. runtime/Play;
12. test integrazione;
13. lifecycle e stress pertinenti;
14. target nativo/WASM pertinente.

L’ordine può variare, ma il modello non deve essere retrofittato alla UI alla fine.

---

## 9. Definition of Done generale

Una feature è completa quando tutti i punti pertinenti sono soddisfatti:

- [ ] autorità definita;
- [ ] nessuna copia autorevole;
- [ ] soluzione minima sufficiente;
- [ ] mutazioni tramite Intent/Command;
- [ ] Command atomico;
- [ ] invarianti nel core;
- [ ] input validato al confine e nel core;
- [ ] Undo corretto;
- [ ] Redo corretto;
- [ ] no-op senza revisione;
- [ ] dirty e revision corretti;
- [ ] Save/Load aggiornati se necessari;
- [ ] migrazione presente se necessaria;
- [ ] Edit/Play isolati;
- [ ] workspace riconciliato;
- [ ] change set/proiezioni coerenti;
- [ ] buffer gestiti;
- [ ] listener e lifetime sicuri;
- [ ] Replace Project gestito;
- [ ] error path implementato;
- [ ] test positivi;
- [ ] test negativi;
- [ ] regression test per bugfix;
- [ ] build pertinente verde;
- [ ] nessun warning critico;
- [ ] nessun segreto;
- [ ] documentazione aggiornata.

---

## 10. Gate automatico dei confini

La CI deve fallire, dove tecnicamente verificabile, se:

- core include RmlUi;
- core include Raylib;
- runtime include header UI;
- persistence include RmlUi;
- Command include `Rml::Element`;
- controller riceve `ProjectDocument&` mutabile;
- render backend include `ProjectDocument`;
- serializer include API UI;
- pannello accede al filesystem concreto;
- viene introdotto singleton mutabile applicativo;
- compare dipendenza circolare;
- un Asset Catalog mutabile duplica le definizioni del documento.

Strumenti ammessi:

- CMake target graph;
- include path;
- script CI;
- clang-tidy;
- dependency graph;
- grep strutturali mirati.

Non creare un nuovo target soltanto per soddisfare formalmente il gate se un controllo più semplice protegge già il confine.

---

## 11. Gate invarianti del dominio

Testare almeno, quando coinvolti:

- ID duplicati rifiutati;
- riferimento mancante gestito;
- `defaultLayerId` valido;
- `EntityInstance.layerId` valido;
- layer appartenente alla scena corretta;
- active scene normalizzata;
- selection non pendente;
- rinomina senza cambio ID;
- valori `NaN`/infinito rifiutati;
- valori fuori range rifiutati;
- movement driver incompatibili rifiutati;
- Object Type/prefab duplicato per nome rifiutato secondo policy;
- cancellazione con riferimenti dipendenti gestita;
- asset mancante con policy definita.

### Blocco P0

Una nuova invariante non può essere protetta soltanto disabilitando un pulsante.

---

## 12. Gate Command/Undo/Redo

Per ogni Command:

- [ ] apply produce lo stato atteso;
- [ ] apply fallito non muta;
- [ ] no-op non crea revisione;
- [ ] Undo ripristina esattamente;
- [ ] Redo riproduce;
- [ ] nuovo Command dopo Undo cancella Redo;
- [ ] delete/undo preservano ordine e indice;
- [ ] precondizioni rivalidate all’esecuzione;
- [ ] errori tipizzati;
- [ ] nessuna dipendenza UI;
- [ ] change set corretto;
- [ ] workspace riconciliato se necessario.

Test minimi:

```text
Apply → New State
Apply → Undo → Original State
Apply → Undo → Redo → New State
Apply → Undo → New Command → No Redo
Failed Apply → No Mutation
No-op → No Revision
```

### Composite Command

- [ ] realmente necessario;
- [ ] operazione indivisibile;
- [ ] rollback o staging testato;
- [ ] fallimento intermedio non lascia stato parziale;
- [ ] validazione/refresh non trasformati artificialmente in Command.

---

## 13. Gate revision e dirty state

- [ ] ogni history entry contiene revision before/after;
- [ ] le revisioni non vengono riutilizzate dopo branching;
- [ ] Undo ripristina la revisione corretta;
- [ ] Redo ripristina la revisione corretta;
- [ ] `isDirty` usa current vs saved revision;
- [ ] workspace non produce dirty;
- [ ] save di snapshot `R` aggiorna `savedRevision` a `R`, non alla revisione corrente successiva;
- [ ] modifica durante salvataggio mantiene dirty;
- [ ] salvataggio fallito non modifica saved state.

Testare esplicitamente:

```text
Save at R
Edit to R+1 while save completes
Result: savedRevision == R, project remains dirty
```

---

## 14. Gate persistenza

Per ogni modifica persistente:

- [ ] schema aggiornato se incompatibile;
- [ ] serializer aggiornato;
- [ ] deserializer aggiornato;
- [ ] migrazione presente;
- [ ] round-trip test;
- [ ] invalid data test;
- [ ] future schema rifiutato;
- [ ] file troncato gestito;
- [ ] salvataggio atomico;
- [ ] load fallito lascia il progetto corrente intatto;
- [ ] RmlUi non partecipa alla serializzazione;
- [ ] stato runtime escluso;
- [ ] workspace escluso salvo specifica separata.

Test minimi:

- progetto vuoto;
- progetto complesso;
- Unicode;
- campi opzionali mancanti;
- ID duplicati;
- riferimenti mancanti;
- JSON malformato;
- file troncato;
- schema precedente;
- schema futuro;
- errore I/O;
- permesso negato;
- file temporaneo esistente;
- failure prima dell’atomic replace.

---

## 15. Gate pending edits e unsaved guard

Testare:

- buffer valido prima di Save/Open/Close;
- buffer incompleto;
- buffer invalido;
- Enter/blur prima dell’unsaved guard;
- Save riuscito;
- Save fallito;
- Discard;
- Cancel.

Verificare:

- [ ] pending edits risolti prima dell’unsaved guard;
- [ ] valori validi committati tramite Command secondo policy;
- [ ] valori invalidi non scartati silenziosamente;
- [ ] la modale restituisce una decisione;
- [ ] la modale non esegue direttamente `replaceProject`;
- [ ] Save fallito blocca l’operazione successiva.

---

## 16. Gate Replace Project

### Prepare

- [ ] parsing/migrazione/validazione su candidato separato;
- [ ] fallimento conserva progetto corrente;
- [ ] nessuna mutazione di UI/domain corrente durante prepare;
- [ ] risorse candidate in staging quando necessario.

### Commit

- [ ] breve e progettato per non fallire;
- [ ] nuovi Command bloccati;
- [ ] Play fermato in sicurezza;
- [ ] nuovo `ProjectSessionId`;
- [ ] job precedenti invalidati;
- [ ] documento sostituito;
- [ ] CommandStack pulito;
- [ ] EditorState normalizzato;
- [ ] selection riconciliata;
- [ ] scene view state ricostruito;
- [ ] cache project-scoped invalidate;
- [ ] `ProjectReplaced` emesso.

### Post-commit

- [ ] proiezioni ricostruite;
- [ ] Data Model ricreati;
- [ ] listener non duplicati;
- [ ] errori UI degradano in modo controllato;
- [ ] capability coerenti.

### Scenari

- Replace valido;
- Replace dopo Stop Play;
- Replace con pannelli aperti;
- Replace con cache popolate;
- Replace con job in corso;
- candidate load fallito;
- errore UI post-commit;
- 100 cicli in suite stress/nightly.

---

## 17. Gate runtime materialization

- [ ] input è `ProjectDocument` const;
- [ ] output è runtime snapshot/struttura indipendente;
- [ ] nessun puntatore mutabile al documento;
- [ ] componenti e asset risolti una volta o con policy esplicita;
- [ ] errori lasciano l’editor in Edit;
- [ ] runtime non include `EditorState`;
- [ ] snapshot testabile senza RmlUi;
- [ ] materiali mancanti/asset mancanti gestiti;
- [ ] target scena risolto chiaramente.

---

## 18. Gate Edit/Play

- [ ] Start usa materializzazione separata;
- [ ] PlaySession possiede il runtime;
- [ ] authoring Command bloccati in Play;
- [ ] workspace può navigare senza mutare runtime;
- [ ] runtime non interroga il documento per frame;
- [ ] Save non include runtime;
- [ ] Stop dispone timer/subscription/script;
- [ ] Stop pulisce cache play-scoped;
- [ ] Start fallito resta in Edit;
- [ ] Stop durante callback è sicuro;
- [ ] Replace Project ferma Play;
- [ ] nessuna contaminazione runtime → authoring.

Scenari:

- Start/Stop ripetuti;
- Start con zero scene/progetto invalido;
- cambio active scene workspace durante Play;
- mutazione runtime e verifica authoring invariato;
- Stop da errore Lua;
- Stop durante evento.

---

## 19. Gate RmlUi contract

Per ogni documento RML significativo:

1. caricare il file;
2. verificare errori parser;
3. verificare ID obbligatori;
4. verificare tipo degli elementi quando rilevante;
5. verificare assenza di ID duplicati;
6. eseguire attach;
7. aggiornare la vista;
8. eseguire detach;
9. chiudere il documento;
10. verificare assenza di callback residue o accessi invalidi.

Scenari:

- RML mancante;
- RCSS mancante;
- elemento obbligatorio assente;
- font mancante;
- reload se supportato;
- finestra piccola;
- DPI elevato;
- testi lunghi.

Un pannello semplice non richiede una gerarchia di test artificiale: il contratto deve coprire il rischio reale.

---

## 20. Gate listener e lifecycle RmlUi

Per ogni controller/sessione:

- [ ] listener posseduti da scope;
- [ ] nessuna cattura di riferimento locale;
- [ ] `this` non sopravvive al controller;
- [ ] detach idempotente;
- [ ] listener rimossi mentre gli elementi sono validi, nel protocollo preferito;
- [ ] se Close precede detach, listener mantenuti vivi fino alla distruzione differita;
- [ ] handle invalidati;
- [ ] reload non duplica listener;
- [ ] Replace Project non lascia callback;
- [ ] modali chiuse non ricevono eventi;
- [ ] shutdown senza use-after-free;
- [ ] custom interfaces vive fino a shutdown RmlUi completo.

Stress nightly/release:

- open/close pannello × 1.000;
- reload RML × 100 se supportato;
- modale × 1.000;
- Replace Project × 100.

Monitorare listener count, memory leak e texture residue.

---

## 21. Gate complessità dei pannelli

Per ogni pannello:

- [ ] la struttura scelta è proporzionata;
- [ ] pannello banale non separato inutilmente in View/Controller/Port;
- [ ] pannello complesso non concentra dominio, lifecycle e rendering in una sola classe;
- [ ] nessun accesso mutabile al documento;
- [ ] nessun filesystem concreto;
- [ ] nessuna logica duplicata;
- [ ] cleanup chiaro.

Domanda di review:

> Questa separazione riduce davvero rischio e complessità, oppure aggiunge soltanto file?

---

## 22. Gate Data Model e Read Model

- [ ] Data Model contiene dati derivati;
- [ ] nessun riferimento mutabile al dominio;
- [ ] niente binding bidirezionale persistente;
- [ ] aggiornamenti programmatici non generano Command ricorsivi;
- [ ] Replace Project ricrea binding project-scoped;
- [ ] niente `DirtyAllVariables()` per frame;
- [ ] errori binding loggati;
- [ ] ViewModel proporzionato alla vista;
- [ ] niente mega-snapshot dell’intero editor;
- [ ] perdita della proiezione non perde dati.

Testare:

```text
EditorChange → projection update → RmlUi update
```

senza generare un nuovo Command.

---

## 23. Gate buffer dei campi

Per ogni campo editabile:

- [ ] policy input/commit/rollback;
- [ ] valori incompleti locali;
- [ ] valori invalidi senza Command;
- [ ] Enter definito;
- [ ] Escape definito;
- [ ] blur definito;
- [ ] cambio selection definito;
- [ ] Start/Stop definito;
- [ ] Replace Project definito;
- [ ] update esterno non sovrascrive silenziosamente il buffer.

Test minimi:

- stringa vuota;
- `-`;
- `.`;
- `12.`;
- `NaN`;
- infinito;
- caratteri non numerici;
- suffisso come `12px`;
- valore fuori range;
- cambio selection durante editing.

---

## 24. Gate input routing

- [ ] input fisico tradotto una volta;
- [ ] focus RmlUi rispettato;
- [ ] Scene View non reagisce a click UI;
- [ ] runtime non riceve input quando UI ha focus;
- [ ] shortcut rispettano input testo;
- [ ] Delete non cancella durante typing;
- [ ] modali bloccano input sottostante;
- [ ] drag capture rilasciato su perdita focus;
- [ ] DPI/scaling corretti;
- [ ] nessun doppio input per frame.

Testare tastiera, mouse, modali, menu, drag, focus e Play.

---

## 25. Gate Scene View

- [ ] una sola camera;
- [ ] una sola conversione world/screen;
- [ ] grid condivisa;
- [ ] snap condiviso;
- [ ] picking coerente;
- [ ] overlay usa stessa camera;
- [ ] draw non muta il dominio;
- [ ] snapshot ricostruibile;
- [ ] pan/zoom/grid/snap non rendono dirty;
- [ ] griglia nascosta non disattiva automaticamente Snap;
- [ ] grid size workspace per scena;
- [ ] Command riceve posizione finale;
- [ ] cache scene-scoped invalidata correttamente;
- [ ] nessuna scansione completa evitabile per frame.

---

## 26. Gate render backend RmlUi/Raylib

- [ ] texture ownership definita;
- [ ] nessun double unload;
- [ ] handle validati;
- [ ] scissor corretto;
- [ ] DPI corretto;
- [ ] resize corretto;
- [ ] stato Raylib ripristinato;
- [ ] placeholder su texture invalida;
- [ ] shutdown corretto;
- [ ] nessun accesso al dominio;
- [ ] `Context::Update()` dopo gli update UI del frame;
- [ ] `Context::Render()` una volta per frame.

Test pertinenti:

- resize continuo;
- DPI diversi;
- clip annidati;
- texture mancante;
- reload UI;
- shutdown/restart host in test.

---

## 27. Gate drag and drop

- [ ] payload tipizzato;
- [ ] nessun puntatore;
- [ ] nessun `Element*`;
- [ ] target rivalidato;
- [ ] ID ancora esistenti;
- [ ] capability rispettate;
- [ ] drop fallito non muta;
- [ ] menu contestuale e DnD usano la stessa operation path;
- [ ] perdita focus annulla;
- [ ] parent non intercetta il child target erroneamente;
- [ ] preview non persistente;
- [ ] operazione finale passa da Intent/Command.

---

## 28. Gate asset

- [ ] definizioni asset soltanto nel `ProjectDocument`;
- [ ] nessun catalogo mutabile duplicato;
- [ ] cache ricostruibili;
- [ ] resolver unico;
- [ ] source path non usato come identità;
- [ ] rename/move conserva `AssetId` quando previsto;
- [ ] delete gestisce riferimenti;
- [ ] import valida formato reale e limiti;
- [ ] placeholder e diagnostica su asset mancante;
- [ ] Replace Project pulisce cache project-scoped;
- [ ] risultato async obsoleto scartato.

Scenari asincroni:

- thumbnail termina dopo delete;
- decode termina dopo Replace Project;
- due import dello stesso asset completano fuori ordine.

---

## 29. Gate thread e job

Applicabile solo se esistono worker/job.

- [ ] dominio nel main thread;
- [ ] RmlUi nel main thread;
- [ ] grafica nel thread richiesto;
- [ ] worker restituisce dati immutabili;
- [ ] completamento passa dal main thread;
- [ ] mutazione persistente convertita in Command;
- [ ] cache derivata aggiornata senza creare autorità;
- [ ] ogni job ha cancellation token;
- [ ] ogni job project-scoped ha `ProjectSessionId`;
- [ ] source revision/version stamp presente;
- [ ] risultati stale scartati;
- [ ] shutdown cancella/attende in modo deterministico;
- [ ] nessuna callback verso oggetti distrutti.

### Gate di necessità

- [ ] esiste una misura di blocco o costo reale;
- [ ] la soluzione sincrona semplice non è sufficiente;
- [ ] il beneficio giustifica cancellation, staleness e lifecycle aggiuntivi.

---

## 30. Gate Lua e Logic Board

- [ ] sandbox attiva;
- [ ] API allowlist;
- [ ] `apiVersion` verificata;
- [ ] scope per board;
- [ ] token cancellabili;
- [ ] dispatch deterministico;
- [ ] snapshot dispatch quando necessario;
- [ ] error isolation;
- [ ] event-only senza tick;
- [ ] helper feature-gated;
- [ ] output compiler deterministico;
- [ ] codice generato read-only;
- [ ] `context.owner` usato quando appropriato;
- [ ] nessun accesso editor UI;
- [ ] budget e limiti definiti.

Test negativi:

- loop o budget superato;
- callback con errore;
- listener che si rimuove durante dispatch;
- evento ricorsivo;
- API version futura;
- accesso filesystem negato;
- memoria oltre quota;
- troppi timer/subscription;
- Stop con timer attivi.

---

## 31. Gate sicurezza

### Sempre applicabili

- [ ] input validato;
- [ ] path canonicalizzato quando usato;
- [ ] nessun segreto nel repository;
- [ ] log senza token/chiavi;
- [ ] testo utente non interpretato come markup;
- [ ] Lua senza accesso non autorizzato;
- [ ] limiti su file e asset.

### Condizionali

#### Archivi

- [ ] Zip Slip bloccato;
- [ ] archive bomb limitata;
- [ ] quantità/dimensione file limitata.

#### Plugin

- [ ] modello permessi;
- [ ] firma/integrità se richiesta;
- [ ] isolamento e revoca.

#### Updater

- [ ] autenticità;
- [ ] integrità;
- [ ] rollback.

#### Rete/cloud

- [ ] TLS;
- [ ] autenticazione;
- [ ] rate limit;
- [ ] privacy e minimizzazione dati.

Se la superficie non esiste, segnare N/A e non implementare infrastruttura preventiva.

---

## 32. Gate limiti delle risorse

Definire limiti per le superfici presenti:

- dimensione progetto;
- scene;
- entità;
- layer;
- Object Type;
- asset;
- texture;
- audio;
- elementi RML dinamici;
- listener;
- timer;
- subscription;
- eventi per frame;
- nodi Logic Board;
- payload DnD;
- stringhe/nomi;
- archivi se supportati.

Il superamento produce errore controllato, non crash, freeze indefinito o allocazione incontrollata.

I valori concreti possono essere aumentati con misure e test; non devono essere numeri arbitrariamente bassi.

---

## 33. Gate performance

### Principi

- nessuna scansione completa del progetto per frame;
- nessuna serializzazione per dirty detection;
- nessun polling per sincronizzazione;
- `Context::Update()` e `Render()` una volta per frame;
- memoria stabile su cicli ripetuti;
- nessuna crescita di listener, texture o callback;
- ottimizzare dopo misura;
- non introdurre copie autorevoli per performance.

### Budget iniziali orientativi

- interazioni UI normali vicine al budget frame;
- operazioni sincrone percepibilmente lunghe con feedback o offload;
- nessun blocco indefinito;
- tempi e soglie documentati per dataset di riferimento.

Le soglie definitive devono derivare da benchmark reali, non da dogmi.

### Scenari

- molte entità;
- molte texture;
- hierarchy profonda;
- Inspector complesso;
- Logic Board estesa;
- sessioni lunghe;
- resize;
- import ripetuti;
- Undo/Redo massivo;
- Start/Stop massivo.

---

## 34. Gate nativo/WASM

### Ambito WASM

Il gate WASM si applica a:

- runtime;
- modello runtime condiviso;
- Logic Runtime;
- asset pipeline condivisa;
- serializer/export usato dal target;
- gameplay API.

Non si applica automaticamente a pannelli RmlUi nativi, docking o layout editoriale, salvo futura decisione esplicita.

### Verifiche pertinenti

- [ ] API piattaforma dietro adapter;
- [ ] path portabili;
- [ ] case sensitivity;
- [ ] filesystem browser;
- [ ] salvataggio asincrono;
- [ ] focus/input browser;
- [ ] audio user gesture;
- [ ] memoria limitata;
- [ ] threading non assunto;
- [ ] runtime senza RmlUi;
- [ ] feature testata sul target dichiarato.

Non dichiarare supporto senza prova.

---

## 35. Test pyramid

### Unit test

- invarianti;
- parser;
- validatori;
- Command;
- CommandStack;
- revisioni;
- migrazioni;
- serializer;
- ID;
- resolver;
- Logic Board compiler;
- proiezioni;
- capability/policy;
- riconciliazione workspace;
- runtime materializer.

### Integration test

- Coordinator + CommandStack;
- Save/Load;
- Replace Project;
- PlaySession;
- asset resolution;
- Lua runtime;
- controller RmlUi con host di test;
- Data Model;
- lifecycle documenti;
- input routing;
- job completion quando presente.

### End-to-end

- nuovo progetto;
- crea scena;
- crea entità;
- modifica componenti;
- salva;
- riapri;
- Undo/Redo;
- Play/Stop;
- import asset;
- unsaved guard;
- progetto corrotto;
- RML mancante;
- Save/Discard/Cancel.

---

## 36. Regression test

Ogni bug corretto deve avere un test che fallisce prima della patch e passa dopo.

Eccezioni:

- bug esclusivamente visivo non automatizzabile;
- test manuale riproducibile registrato;
- task per automatizzazione futura quando ragionevole.

Non cambiare l’aspettativa di un test soltanto per adeguarla alla nuova implementazione senza verificare il comportamento desiderato.

---

## 37. Livelli CI

### Locale / ogni slice

- build target coinvolti;
- test core coinvolti;
- regression test;
- Command test se pertinente;
- serializer test se pertinente;
- lint/format leggero.

### Pull request

- build pulita editor/core/runtime;
- unit test;
- integrazione principali;
- RML contract pertinenti;
- serializer/migrazione;
- compiler Logic Board;
- controlli architetturali;
- static analysis selezionata;
- secret scan.

### Nightly

- sanitizer;
- leak detection;
- stress lifecycle;
- dataset grandi;
- multi-compiler quando disponibile;
- vulnerability scan dipendenze;
- job stale/cancellation;
- Start/Stop e Open/Close massivi.

### Release

- tutti i gate precedenti;
- migrazioni reali da versioni supportate;
- crash recovery;
- compatibilità backup;
- target export dichiarati;
- verifica licenze;
- piano rollback.

I test costosi non devono rallentare ogni micro-slice, ma devono essere obbligatori nella frequenza appropriata.

---

## 38. Gate codice generato dall’AI

Prima di accettare una patch:

1. leggere il piano;
2. verificare i file modificati;
3. leggere il diff completo;
4. cercare API inventate;
5. cercare doppie autorità;
6. cercare polling/sync;
7. cercare manager e astrazioni non necessarie;
8. cercare binding bidirezionali;
9. cercare listener senza cleanup;
10. verificare raw pointer e lifetime;
11. verificare Undo/Redo;
12. verificare Save/Load;
13. verificare Play/Stop;
14. verificare error path;
15. verificare test negativi;
16. verificare dipendenze;
17. compilare e testare.

### Blocco P0

Non accettare:

- refactor massivi non richiesti;
- manager generici;
- TODO nascosti;
- fallback silenziosi;
- test indeboliti;
- API inesistenti;
- temporary sync;
- quick fix che aggira il Coordinator;
- job system o target introdotti senza necessità;
- codice non compreso dal revisore.

---

## 39. Gate code review

Ogni review risponde:

- Qual è l’autorità?
- Dove avviene la mutazione?
- Il Command è realmente necessario e atomico?
- Esiste una logica equivalente?
- Il modulo è corretto?
- La soluzione è la più semplice sufficiente?
- Undo/Redo sono completi?
- Dirty/revision sono corretti?
- Persistenza e migrazione sono coerenti?
- Cosa accade durante Play?
- Chi possiede le risorse?
- Cosa accade su detach/close/replace/shutdown?
- Gli errori sono espliciti?
- I test coprono failure path?
- La modifica crea coupling o stato duplicato?
- Le parti opzionali sono davvero necessarie?

---

## 40. Gate pull request

Ogni PR sostanziale include:

- problema;
- soluzione;
- autorità coinvolte;
- moduli coinvolti;
- invarianti;
- Intent/Command;
- Undo/Redo;
- persistenza/migrazione;
- lifecycle;
- test;
- rischi;
- motivazione delle nuove astrazioni;
- rollback.

Una PR non mescola:

- feature;
- refactor non necessario;
- formattazione globale;
- upgrade dipendenze non correlato.

Screenshot supportano la review ma non sostituiscono i test.

---

## 41. Gate release

Prima di una release:

- [ ] tutte le P0 chiuse;
- [ ] nessun test disabilitato senza ticket;
- [ ] Save/Load su progetti reali verificato;
- [ ] migrazioni supportate verificate;
- [ ] crash/error recovery verificato;
- [ ] Start/Stop stress verde;
- [ ] Open/Close project stress verde;
- [ ] RML contract suite verde;
- [ ] leak/sanitizer pertinenti verdi;
- [ ] security scan verde;
- [ ] licenze dipendenze verificate;
- [ ] build nativa pulita;
- [ ] target export dichiarati verdi;
- [ ] note release;
- [ ] piano rollback;
- [ ] compatibilità backup verificata.

---

## 42. Gate debito tecnico

Un debito accettato contiene:

- descrizione;
- motivazione;
- rischio;
- componente;
- workaround;
- owner;
- priorità;
- condizione/data di revisione;
- test che impedisce peggioramenti.

Non sono debito accettabile:

- doppia autorità;
- perdita dati;
- vulnerabilità critica;
- lifetime indefinito;
- migrazione necessaria assente;
- salvataggio non atomico;
- runtime che modifica authoring;
- errore critico ignorato.

---

## 43. Gate ADR

Serve ADR per:

- nuova autorità;
- cambio formato progetto;
- nuova policy Play;
- plugin system;
- hot reload supportato;
- multi-threading del core;
- più contesti RmlUi;
- sistema temi estendibile dal progetto;
- telemetria;
- updater;
- Apply Runtime Changes;
- nuova dipendenza core significativa.

Non serve ADR per ogni piccola classe o refactor locale.

Un ADR contiene:

- contesto;
- problema;
- decisione;
- alternative;
- conseguenze;
- vincoli;
- stato;
- data.

---

## 44. Checklist pre-merge compatta

```text
[ ] Autorità unica
[ ] Soluzione minima sufficiente
[ ] Modulo corretto
[ ] Nessuna dipendenza vietata
[ ] Intent/Command rispettati
[ ] Command atomico
[ ] Invarianti nel core
[ ] Undo/Redo corretti
[ ] Dirty/revision corretti
[ ] Save/Load corretti
[ ] Migrazione gestita
[ ] Pending edits/unsaved guard corretti
[ ] Replace Project sicuro
[ ] Edit/Play isolati
[ ] Runtime snapshot indipendente
[ ] Workspace riconciliato
[ ] Read Model mirati
[ ] Buffer gestiti
[ ] Listener/lifecycle sicuri
[ ] Errori espliciti
[ ] Input validati
[ ] Path sicuri
[ ] Job stale scartati, se presenti
[ ] Test positivi/negativi
[ ] Regression test
[ ] Build pertinente verde
[ ] WASM verificato se pertinente
[ ] Nessun segreto
[ ] Diff AI compreso e revisionato
```

---

## 45. Stop-the-line operativo

Bloccare immediatamente se appare:

- mutazione diretta dal pannello;
- Data Model come store;
- doppio Asset Catalog;
- `LayerManager` che duplica `SceneDef.layers`;
- polling/sync;
- `RefreshAll()` usato per nascondere incoerenze;
- dipendenza circolare;
- runtime che modifica authoring;
- worker che modifica il dominio;
- risultato async stale applicato;
- listener senza scope;
- use-after-unload potenziale;
- salvataggio non atomico;
- progetto corrotto convertito in vuoto;
- unsaved guard dentro `replaceProject`;
- Lua con accesso non autorizzato;
- path traversal;
- test rimossi o indeboliti;
- errore critico nascosto da `if (!x) return`;
- operazione parzialmente applicata;
- complessità senza problema concreto;
- patch AI non compresa.

---

## 46. Criterio di accettazione finale

Una modifica è accettabile soltanto quando:

- funziona;
- rispetta l’architettura consolidata;
- usa la soluzione più semplice sufficiente;
- non crea doppie autorità;
- è annullabile quando necessario;
- è persistibile quando necessario;
- fallisce in modo sicuro;
- non contamina Edit e Play;
- ha lifetime chiari;
- è coperta da test significativi;
- rimane comprensibile a un revisore umano.
