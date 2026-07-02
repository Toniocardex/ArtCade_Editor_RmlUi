# ArtCade — gestione dei font con RmlUi

Queste linee guida devono essere applicate durante il refactor dell’editor nativo ArtCade basato su RmlUi.

L’obiettivo è ottenere un sistema font:

- completamente locale;
- riproducibile;
- indipendente da Google Fonts, CDN o connessione Internet;
- compatibile con distribuzione commerciale;
- stabile tra macchine diverse;
- semplice da mantenere;
- privo di `FontManager`, servizi di sincronizzazione o astrazioni inutili.

---

# 1. Principio generale

RmlUi non deve dipendere da font installati nel sistema operativo.

Il font principale dell’editor deve essere:

- incluso fisicamente nel repository;
- versionato;
- copiato nel pacchetto finale;
- caricato esplicitamente durante il bootstrap della UI;
- accompagnato dalla relativa licenza.

Non usare:

- Google Fonts via rete;
- CDN;
- download a runtime;
- font di sistema come dipendenza primaria;
- fallback silenziosi verso font non controllati;
- path relativi dipendenti dalla working directory.

La UI deve avere lo stesso rendering tipografico su ogni macchina supportata.

---

# 2. Font consigliato

Usare **Inter** come font principale dell’editor.

Motivazioni:

- alta leggibilità a dimensioni ridotte;
- adatto a Inspector, Hierarchy, toolbar, tab e console;
- disponibile in più pesi;
- buona resa in interfacce tecniche dense;
- distribuibile localmente con licenza permissiva per font.

Usare inizialmente file statici separati, non il variable font.

Pesi consigliati:

```text
Inter Regular   400
Inter Medium    500
Inter SemiBold  600
Inter Bold      700
```

Non includere pesi non utilizzati.

---

# 3. Struttura delle risorse

Usare una struttura chiara e poco profonda:

```text
editor-native/
└── resources/
    └── fonts/
        └── inter/
            ├── Inter-Regular.ttf
            ├── Inter-Medium.ttf
            ├── Inter-SemiBold.ttf
            ├── Inter-Bold.ttf
            └── LICENSE.txt
```

Nel pacchetto finale:

```text
ArtCade/
├── ArtCade.exe
├── resources/
│   └── fonts/
│       └── inter/
└── THIRD_PARTY_NOTICES.md
```

Non rinominare i file con nomi ambigui come:

```text
font.ttf
main.ttf
ui.ttf
regular.ttf
```

Mantenere nomi espliciti e coerenti.

---

# 4. Caricamento dei font

Creare due file con responsabilità chiara:

```text
ui/editor_fonts.h
ui/editor_fonts.cpp
```

Evitare nomi come:

```text
font_manager.cpp
font_service.cpp
font_utils.cpp
font_loader_manager.cpp
```

Non serve un manager. Basta una funzione di bootstrap specifica.

## `editor_fonts.h`

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace artcade::ui {

struct FontLoadResult {
    bool ok = false;
    std::string error;
};

[[nodiscard]]
FontLoadResult loadEditorFonts(
    const std::filesystem::path& resourceRoot
);

} // namespace artcade::ui
```

## `editor_fonts.cpp`

```cpp
#include "editor_fonts.h"

#include <RmlUi/Core.h>

#include <array>
#include <filesystem>
#include <string>

namespace artcade::ui {
namespace {

struct RequiredFont {
    const char* relativePath;
    const char* label;
};

constexpr std::array<RequiredFont, 4> kRequiredFonts{{
    {"fonts/inter/Inter-Regular.ttf",  "Inter Regular"},
    {"fonts/inter/Inter-Medium.ttf",   "Inter Medium"},
    {"fonts/inter/Inter-SemiBold.ttf", "Inter SemiBold"},
    {"fonts/inter/Inter-Bold.ttf",     "Inter Bold"},
}};

} // namespace

FontLoadResult loadEditorFonts(
    const std::filesystem::path& resourceRoot
) {
    for (const RequiredFont& font : kRequiredFonts) {
        const std::filesystem::path path =
            resourceRoot / font.relativePath;

        if (!std::filesystem::exists(path)) {
            return {
                false,
                std::string("Missing required font: ") +
                    font.label +
                    " (" + path.string() + ")"
            };
        }

        if (!Rml::LoadFontFace(path.string())) {
            return {
                false,
                std::string("RmlUi failed to load font: ") +
                    font.label +
                    " (" + path.string() + ")"
            };
        }
    }

    return {true, {}};
}

} // namespace artcade::ui
```

---

# 5. Inizializzazione

Caricare i font una sola volta durante l’inizializzazione RmlUi.

Flusso consigliato:

```text
EditorApp
→ risolve ResourceRoot
→ inizializza RmlUi
→ carica i font
→ crea il Context
→ carica RML e RCSS
```

Esempio:

```cpp
const auto fontResult =
    artcade::ui::loadEditorFonts(resourceRoot);

if (!fontResult.ok) {
    logError(fontResult.error);
    return false;
}
```

Non:

- caricare i font a ogni frame;
- caricarli da ogni pannello;
- tentare retry automatici;
- continuare con un font non definito;
- ignorare il fallimento.

Se un font obbligatorio manca, l’inizializzazione della UI deve fallire con un errore chiaro.

---

# 6. Resource root

Non usare direttamente:

```cpp
Rml::LoadFontFace(
    "resources/fonts/inter/Inter-Regular.ttf"
);
```

se il comportamento dipende dalla directory da cui viene lanciato l’eseguibile.

Risolvere una sola volta il percorso assoluto della directory risorse:

```text
directory dell’eseguibile
→ resources
→ fonts
```

Esempio concettuale:

```cpp
const std::filesystem::path executableDir =
    platform::executableDirectory();

const std::filesystem::path resourceRoot =
    executableDir / "resources";
```

Passare `resourceRoot` a `loadEditorFonts()`.

Non introdurre un `ResourceSyncService`.

---

# 7. Configurazione RCSS

Definire il font globalmente nel tema.

Esempio:

```css
body {
    font-family: Inter;
    font-size: 14px;
    color: #d8dbe2;
}

.menu-item,
.toolbar-button,
.panel-title {
    font-weight: 500;
}

.section-title,
.primary-button {
    font-weight: 600;
}

.dialog-title {
    font-weight: 700;
}

.console-line,
.numeric-input {
    font-family: Inter;
}
```

Verificare il nome effettivo della famiglia registrata dai metadati del font.

Se RmlUi registra una famiglia diversa da `Inter`, correggere l’RCSS sulla base dei log reali.

Non inventare alias lato applicazione se non strettamente necessario.

---

# 8. Fallback Unicode

Aggiungere un font fallback soltanto se serve davvero.

Scelta consigliata:

```text
Noto Sans
```

Struttura:

```text
resources/fonts/
├── inter/
│   ├── Inter-Regular.ttf
│   ├── Inter-Medium.ttf
│   ├── Inter-SemiBold.ttf
│   ├── Inter-Bold.ttf
│   └── LICENSE.txt
└── noto/
    ├── NotoSans-Regular.ttf
    └── LICENSE.txt
```

Caricamento concettuale:

```cpp
Rml::LoadFontFace(
    (resourceRoot / "fonts/noto/NotoSans-Regular.ttf").string(),
    true
);
```

Il fallback deve essere caricato dopo il font principale.

Usarlo per:

- caratteri italiani;
- simboli mancanti;
- alfabeti non coperti da Inter;
- testi internazionali futuri.

Non includere intere famiglie Noto non utilizzate.

---

# 9. Icone

Non usare caratteri Unicode o emoji come sistema principale di icone.

Per toolbar e controlli usare preferibilmente:

```text
SVG
oppure
texture atlas
```

Motivazioni:

- resa coerente;
- nessuna dipendenza dal font di sistema;
- nessuna differenza tra piattaforme;
- controllo preciso di dimensioni e allineamento;
- stile visivo uniforme.

Non usare un font icon proprietario senza licenza chiara.

---

# 10. Font incorporati nell’eseguibile

RmlUi può anche caricare un font dalla memoria.

Questa opzione può essere valutata in seguito per il packaging finale:

```text
font incorporato nell’eseguibile
→ nessun file mancante
→ nessun path esterno
```

Non usarla nello spike iniziale.

Durante lo spike mantenere i file font esterni perché:

- sono più semplici da sostituire;
- sono più facili da diagnosticare;
- rendono chiari i problemi di packaging;
- evitano generatori di header o conversioni binarie premature.

Valutare l’incorporamento solo dopo che:

- il caricamento da file è stabile;
- le licenze sono state verificate;
- il packaging finale è definito;
- è dimostrato un beneficio reale.

---

# 11. Build e packaging

Il build del nuovo editor deve copiare automaticamente:

```text
RML
RCSS
font
icone
licenze
```

nella directory di output.

Esempio atteso:

```text
build/editor-native/
├── ArtCade.exe
└── resources/
    ├── ui/
    ├── fonts/
    └── icons/
```

Da clean checkout, il nuovo target deve poter essere compilato e avviato senza operazioni manuali aggiuntive.

Aggiungere un controllo post-build o uno smoke test che verifichi l’esistenza di:

```text
resources/fonts/inter/Inter-Regular.ttf
resources/fonts/inter/Inter-Medium.ttf
resources/fonts/inter/Inter-SemiBold.ttf
resources/fonts/inter/Inter-Bold.ttf
```

---

# 12. Licenze

Conservare nel repository e nel pacchetto:

```text
resources/fonts/inter/LICENSE.txt
resources/fonts/noto/LICENSE.txt
THIRD_PARTY_NOTICES.md
```

Nel file `THIRD_PARTY_NOTICES.md` indicare almeno:

```text
Font:
Autore/progetto:
Versione o commit:
Licenza:
Percorso della licenza:
Uso in ArtCade:
```

Non scaricare font da mirror non ufficiali.

Pinare una versione o un commit preciso del font usato.

Non affidarsi a URL dinamici durante la build.

---

# 13. Test minimi

Aggiungere test o verifiche per:

1. `loadEditorFonts()` restituisce errore se manca un font obbligatorio.
2. Il messaggio di errore contiene il percorso del file mancante.
3. Il caricamento riesce con tutti i file presenti.
4. Il resource root è assoluto o risolto rispetto all’eseguibile.
5. I font non vengono caricati più volte.
6. Il context RmlUi non viene creato se il caricamento font fallisce.
7. L’RCSS usa una famiglia realmente caricata.
8. I caratteri italiani vengono renderizzati correttamente.
9. Il fallback Unicode funziona, se abilitato.
10. Il build copia font e licenze nella directory finale.

Checklist manuale:

```text
[ ] menu leggibili
[ ] tab leggibili
[ ] Inspector leggibile
[ ] console leggibile
[ ] font stabile a DPI 100%
[ ] font stabile a DPI 125%
[ ] font stabile a DPI 150%
[ ] nessun fallback di sistema
[ ] nessun carattere mancante
[ ] nessun asset font caricato da rete
```

---

# 14. Regole di semplicità

Non introdurre:

```text
FontManager
FontService
FontRegistry globale
FontSync
FontObserver
FontRepository
FontProvider dinamico
```

La soluzione minima richiesta è:

```text
resourceRoot
→ loadEditorFonts()
→ RmlUi
```

Il sistema font non deve avere:

- polling;
- hot reload automatico;
- sync;
- retry;
- stato duplicato;
- observer;
- callback distribuite.

Il font è una risorsa di bootstrap, non un subsystem applicativo.

---

# 15. Criterio finale

La soluzione è corretta se può essere spiegata così:

```text
ArtCade include Inter nel repository
→ il build copia i file
→ RmlUi li carica all’avvio
→ RCSS usa Inter
→ il pacchetto include le licenze
```

Non aggiungere complessità ulteriore salvo evidenze tecniche concrete.

La scelta consigliata per lo spike è:

```text
Inter statico vendorizzato
+ quattro pesi
+ Noto Sans fallback opzionale
+ icone SVG o atlas
+ licenze incluse
+ caricamento locale deterministico
```
---

# 16. Qualità del rendering del font

Il caricamento corretto del font non garantisce da solo una resa nitida. La qualità finale dipende dall’intera pipeline:

```text
font corretto
+ FreeType correttamente inizializzato
+ framebuffer reale
+ DPI corretto
+ renderer RmlUi corretto
+ blending corretto
+ scissor corretto
+ texture filtering corretto
+ test visivi
```

Claude deve trattare la qualità tipografica come parte dell’integrazione grafica, non soltanto come problema di asset.

Obiettivi obbligatori:

- testo nitido;
- baseline coerente;
- nessun alone;
- nessun clipping;
- nessuna deformazione;
- nessuno scaling post-render;
- comportamento stabile durante resize e cambio DPI;
- resa coerente tra menu, Inspector, Hierarchy e Console.

---

# 17. Dimensioni finestra, framebuffer e DPI

Distinguere sempre:

```text
window size
framebuffer size
DPI scale
RmlUi context dimensions
```

Non assumere che coincidano. Su monitor HiDPI, le dimensioni logiche della finestra possono differire dalle dimensioni fisiche del framebuffer.

Usare una struttura esplicita:

```cpp
struct WindowMetrics {
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    float dpiScale = 1.0f;
};
```

Durante inizializzazione e resize, aggiornare il context RmlUi con le metriche effettive:

```cpp
context->SetDimensions({
    metrics.framebufferWidth,
    metrics.framebufferHeight
});

context->SetDensityIndependentPixelRatio(
    metrics.dpiScale
);
```

Verificare la firma reale dell’API nella versione RmlUi fissata nel repository.

Non confondere:

- coordinate logiche della UI;
- coordinate fisiche del framebuffer;
- coordinate mouse;
- coordinate viewport ArtCade.

---

# 18. Aggiornamento DPI

Il DPI non deve essere hardcoded a `1.0` e non deve essere impostato una sola volta assumendo che non cambi.

Aggiornare le metriche quando:

- la finestra viene ridimensionata;
- cambia il framebuffer;
- la finestra passa su un monitor con DPI differente;
- cambia lo scaling del sistema;
- viene ricreata la swap chain o il render target;
- viene ricevuto l’evento piattaforma equivalente a `DPI changed`.

Flusso richiesto:

```text
evento resize/DPI
→ rileva window size
→ rileva framebuffer size
→ calcola dpiScale
→ aggiorna RmlUi context
→ aggiorna viewport
→ invalida layout
→ frame successivo
```

Non introdurre polling continuo se la piattaforma espone eventi affidabili. Se Raylib o il layer piattaforma non espongono un evento DPI utilizzabile, è ammesso confrontare le metriche a inizio frame e aggiornare il context soltanto quando cambiano realmente. Documentare questa eccezione.

---

# 19. Divieto di scaling post-render della UI

Non renderizzare l’intera UI RmlUi a una risoluzione ridotta per poi ingrandirla tramite texture.

Vietato:

```text
RmlUi a 1280×720
→ RenderTexture
→ scaling a 1920×1080
```

Questo produce testo sfocato anche se il font è corretto.

Preferire:

```text
RmlUi renderizzato direttamente
alla risoluzione fisica del framebuffer
con dp-ratio corretto
```

Una `RenderTexture` può essere usata per il viewport della scena ArtCade, ma non come percorso predefinito dell’intera UI.

---

# 20. Integrazione con il renderer OpenGL/Raylib

RmlUi genera geometria e texture dei glyph, ma la qualità finale dipende dal renderer integrato nell’applicazione.

Usare come riferimento il backend OpenGL 3 ufficiale compatibile con la versione RmlUi adottata. Non riscrivere da zero il rendering dei font nello spike salvo necessità dimostrata.

Prima del rendering RmlUi:

1. completare o flushare il batch Raylib/rlgl;
2. salvare lo stato grafico necessario;
3. impostare il viewport corretto;
4. impostare blending e scissor richiesti;
5. impostare shader, texture e projection della UI;
6. renderizzare RmlUi;
7. ripristinare lo stato necessario per il frame successivo.

Non annidare `BeginDrawing()`.

Non creare un secondo context OpenGL.

Non creare un secondo loop di rendering.

---

# 21. Stato grafico da verificare

Verificare esplicitamente almeno:

- blending;
- funzione di blending;
- texture binding;
- texture filtering;
- active texture unit;
- shader corrente;
- vertex array e buffer;
- viewport;
- scissor test;
- coordinate dello scissor;
- projection matrix;
- depth test;
- culling;
- stencil, se usato;
- premultiplied alpha o straight alpha;
- sRGB, se attivo;
- ordine di rendering tra viewport e UI.

Evitare correzioni casuali dello stato.

Usare una coppia di funzioni locali e specifiche:

```cpp
beginUiRender();
rmlContext->Render();
endUiRender();
```

Queste funzioni devono occuparsi soltanto dello stato grafico della UI. Non creare un generico `GraphicsStateManager`.

---

# 22. Alpha blending

Verificare il tipo di alpha prodotto e atteso dal backend RmlUi adottato.

Sintomi di blending errato:

- aloni scuri intorno ai glyph;
- bordi bianchi o grigi;
- testo troppo sottile;
- testo eccessivamente scuro;
- trasparenza non uniforme;
- rettangoli visibili intorno ai caratteri.

Non correggere questi problemi modificando il colore RCSS del font.

Prima verificare:

- modalità di blending;
- premultiplied alpha;
- formato texture;
- shader;
- conversione sRGB;
- clear color.

---

# 23. Texture filtering

Verificare la configurazione usata dal backend RmlUi per le texture dei font.

Sintomi di filtering errato:

- caratteri sfocati;
- bordi tremolanti;
- testo irregolare durante resize;
- perdita di definizione a dimensioni piccole.

Non forzare globalmente il filtering di tutte le texture Raylib per correggere i font. Le texture UI devono essere configurate nel renderer RmlUi senza alterare quelle del gioco.

---

# 24. Scissor e clipping

Il testo tagliato può derivare da coordinate di scissor errate, non dal font.

Verificare:

- origine delle coordinate;
- conversione asse Y;
- dimensioni fisiche del framebuffer;
- DPI scale;
- viewport corrente;
- rettangoli con larghezza o altezza zero;
- rounding tra float e int;
- clipping dei pannelli;
- clipping degli input.

Non usare lo stesso rettangolo senza conversione per:

```text
coordinate RmlUi
coordinate Raylib
coordinate OpenGL
coordinate framebuffer
```

Centralizzare la conversione in una funzione specifica e testabile:

```cpp
ScissorRect toFramebufferScissor(
    const RmlRect& rect,
    const WindowMetrics& metrics
);
```

---

# 25. Ordine del frame

L’ordine deve essere deterministico:

```text
poll input
→ aggiorna metriche finestra/DPI se cambiate
→ inoltra input a RmlUi
→ Context::Update()
→ render viewport ArtCade
→ render RmlUi
→ present
```

Non chiamare `Render()` prima di `Update()`.

Non aggiornare il layout dopo il rendering.

Non caricare font durante il frame.

Non ricreare il context durante il resize.

Non ricreare i documenti RML per applicare il nuovo DPI.

---

# 26. Dimensioni tipografiche

Durante lo spike usare dimensioni intere e controllate.

Valori iniziali da verificare visivamente:

```text
12 px  testo secondario molto compatto
13 px  console e proprietà dense
14 px  testo UI generale
15 px  tab o controlli principali
16 px  titoli dialog e sezioni importanti
```

Evitare inizialmente:

- dimensioni frazionarie arbitrarie;
- scale CSS globali;
- trasformazioni `scale()` sulla UI;
- zoom del documento RML;
- font sotto 11 px;
- differenze di un solo pixel non motivate tra controlli equivalenti.

---

# 27. Pesi e synthetic bold

Verificare che ogni peso RCSS corrisponda a un file realmente caricato.

Mappatura attesa:

```text
400 → Inter-Regular.ttf
500 → Inter-Medium.ttf
600 → Inter-SemiBold.ttf
700 → Inter-Bold.ttf
```

Non affidarsi a bold sintetico prodotto dal renderer. Se un peso richiesto non esiste, correggere il tema o aggiungere esplicitamente il file corretto.

---

# 28. Variable font

Non usare il variable font nello spike iniziale.

Motivi:

- diagnostica più difficile;
- mapping peso/stile meno immediato;
- maggiore possibilità di differenze tra versioni del font engine;
- nessun beneficio concreto per il primo editor nativo.

Valutarlo soltanto dopo aver stabilizzato caricamento, DPI, rendering, packaging, pesi statici e test visivi.

---

# 29. Hinting e resa a piccole dimensioni

La resa dei font a 12–14 px deve essere verificata su Windows reale.

Controllare:

- vertical stem;
- baseline;
- spaziatura;
- contrasto;
- lettere simili come `I`, `l`, `1`;
- numeri;
- parentesi;
- punteggiatura;
- simboli matematici;
- underscore;
- slash e backslash.

Non modificare FreeType o il font engine nello spike salvo bug dimostrato. Prima verificare DPI, framebuffer, filtering, blending e font size.

---

# 30. Pagina diagnostica font

Creare una pagina diagnostica semplice e rimovibile:

```text
resources/ui/font_test.rml
```

Deve mostrare almeno:

```text
Inter Regular 12 px
Inter Regular 13 px
Inter Regular 14 px
Inter Regular 16 px

Inter Medium 14 px
Inter SemiBold 14 px
Inter Bold 14 px

ABCDEFGHIJKLMNOPQRSTUVWXYZ
abcdefghijklmnopqrstuvwxyz
0123456789

à è é ì ò ù
À È É Ì Ò Ù

() [] {} <> / \ | _ - + = : ; , . ! ?

The quick brown fox jumps over the lazy dog.
Sphinx of black quartz, judge my vow.
```

Aggiungere inoltre:

- testo su sfondi chiari e scuri;
- testo selezionato;
- testo disabled;
- warning;
- error;
- testo dentro input;
- testo dentro tab;
- testo nella console;
- numeri allineati in colonna;
- stringhe lunghe con clipping;
- fallback Unicode, se abilitato.

La pagina diagnostica deve essere disponibile soltanto in build debug o tramite flag di sviluppo.

---

# 31. Test DPI obbligatori

Testare almeno:

```text
100%
125%
150%
200%
```

Per ogni scala verificare:

- nitidezza;
- dimensioni coerenti;
- baseline;
- padding;
- clipping;
- menu;
- toolbar;
- Inspector;
- Console;
- dropdown;
- input;
- tooltip;
- popup;
- selezione;
- scroll;
- splitter;
- viewport overlay.

Testare anche il passaggio della finestra tra due monitor con DPI differenti, quando disponibile.

Durante il passaggio non devono verificarsi:

- testo temporaneamente sfocato;
- UI troppo grande o troppo piccola;
- input disallineato;
- clipping errato;
- viewport con coordinate sbagliate;
- crash;
- ricreazione completa dell’editor.

---

# 32. Test resize obbligatori

Eseguire resize lento e rapido della finestra.

Verificare:

- nessun testo stirato;
- nessun frame con scaling della UI;
- nessun clipping persistente;
- nessuna ricreazione dei font;
- nessuna perdita di texture;
- nessun flicker;
- nessun ritardo tra layout e framebuffer;
- nessun crash con dimensioni minime;
- gestione corretta di larghezza o altezza temporaneamente zero.

Quando il framebuffer ha dimensioni zero, saltare il rendering in modo esplicito senza distruggere context o font.

---

# 33. Logging diagnostico

In build debug, registrare una sola volta:

```text
font file
family registrata
peso
stile
resource root
window size
framebuffer size
dpi scale
backend renderer
```

Esempio:

```text
[RmlUi] Loaded font: Inter Regular
[RmlUi] Loaded font: Inter Medium
[RmlUi] Loaded font: Inter SemiBold
[RmlUi] Loaded font: Inter Bold
[RmlUi] Framebuffer: 1920x1080
[RmlUi] DPI scale: 1.25
```

Non scrivere questi dati ogni frame.

Se il nome della famiglia registrata non coincide con l’RCSS, fallire in debug o mostrare un warning chiaro. Non nascondere il problema con alias automatici non documentati.

---

# 34. Smoke test grafico

Aggiungere una checklist manuale dedicata:

```text
[ ] nessun testo sfocato a 100%
[ ] nessun testo sfocato a 125%
[ ] nessun testo sfocato a 150%
[ ] nessun testo sfocato a 200%
[ ] nessun alone intorno ai glyph
[ ] nessun carattere tagliato
[ ] nessun peso sintetico inatteso
[ ] nessun fallback di sistema
[ ] nessuna UI renderizzata a risoluzione ridotta
[ ] nessuna texture UI scalata dopo il render
[ ] resize senza deformazioni
[ ] cambio monitor senza disallineamento
[ ] Console leggibile
[ ] Inspector leggibile
[ ] input e caret allineati
[ ] menu e popup nitidi
```

Salvare almeno uno screenshot per ogni scala DPI testata nel report dello spike.

---

# 35. Test automatici aggiuntivi

Dove possibile, aggiungere test per:

1. il context riceve la dimensione framebuffer corretta;
2. il context riceve il `dpiScale` corretto;
3. un cambio DPI aggiorna il context una sola volta;
4. nessun aggiornamento avviene se le metriche non cambiano;
5. coordinate mouse e UI vengono convertite correttamente;
6. lo scissor viene convertito nel sistema di coordinate framebuffer;
7. framebuffer zero salta il render senza distruggere il context;
8. il rendering UI non modifica permanentemente lo stato richiesto dal renderer ArtCade;
9. i font non vengono ricaricati durante resize;
10. il path font resta indipendente dalla working directory.

Non introdurre test pixel-perfect fragili nello spike iniziale, salvo che l’infrastruttura di screenshot test esista già.

---

# 36. Criteri di fallimento del rendering font

Considerare l’integrazione non accettabile se si verifica uno dei seguenti casi:

- testo visibilmente sfocato a DPI standard;
- UI renderizzata a bassa risoluzione e poi scalata;
- dipendenza da font di sistema;
- caricamento font ripetuto;
- font mancanti ignorati;
- DPI hardcoded a `1.0`;
- window size usata sempre come framebuffer size;
- aloni da blending errato;
- clipping persistente;
- passaggio monitor DPI non gestito;
- uso di variable font per aggirare problemi di peso;
- modifica globale del filtering Raylib per correggere RmlUi;
- ricreazione del context a ogni resize;
- fallback silenzioso a una famiglia diversa;
- renderer font riscritto da zero senza necessità.

---

# 37. Deliverable per Claude

Aggiornare il report dello spike con una sezione:

```text
Font rendering validation
```

Includere:

```text
Font principale:
Pesi caricati:
Fallback:
Versione/commit:
Font engine:
Backend grafico:
Window size testata:
Framebuffer size testata:
DPI testati:
Problemi trovati:
Correzioni applicate:
Screenshot prodotti:
Esito:
```

L’esito deve essere uno tra:

```text
PASS
PASS WITH LIMITATIONS
FAIL
```

Non dichiarare `PASS` basandosi soltanto sul fatto che il testo appare a schermo. Verificare almeno nitidezza, DPI, resize, clipping, blending e packaging.

---

# 38. Criterio finale aggiornato

La soluzione completa deve poter essere spiegata così:

```text
ArtCade include Inter nel repository
→ il build copia font e licenze
→ il bootstrap risolve resourceRoot
→ RmlUi carica i pesi statici una sola volta
→ il context usa framebuffer e DPI reali
→ il backend OpenGL renderizza la UI alla risoluzione finale
→ RCSS usa famiglia e pesi caricati
→ test DPI, resize e clipping verificano la qualità
```

La strategia definitiva per lo spike è:

```text
Inter statico vendorizzato
+ quattro pesi
+ Noto Sans fallback opzionale
+ icone SVG o atlas
+ licenze incluse
+ resource root assoluto
+ framebuffer reale
+ dp-ratio corretto
+ backend OpenGL 3 adattato
+ nessuno scaling post-render
+ test DPI 100/125/150/200%
+ pagina diagnostica font
```

Non aggiungere un subsystem font più complesso di quanto richiesto da questo flusso.
