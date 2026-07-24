# ArtCade SFX Synthesizer v2

Modulo C++17 pensato per l'integrazione nel game engine/editor ArtCade.
Il core genera un buffer mono `float` deterministico; preview, serializzazione ed
encoding sono adapter separati.

## Miglioramenti rispetto al prototipo

- ADSR validato: attack + decay + release non possono superare la durata.
- De-click finale: primo e ultimo campione a zero.
- `Square` sempre al 50%; `Pulse` con duty configurabile.
- DC blocker per pulse asimmetriche e mix con offset.
- Noise con clock realmente controllato dal pitch sweep.
- Bit crusher espresso in Hz, indipendente dal sample rate del progetto.
- Sweep lineare o esponenziale.
- Oscillatori raw o band-limited PolyBLEP per square, pulse e saw.
- Due voci tonali più layer noise.
- Buffer interno `float`, conversione PCM16 solo nell'encoder/adapter.
- Recipe con `schemaVersion`, `generatorVersion` e seed deterministico.
- WAV nativo; Ogg Vorbis diretto opzionale tramite libogg/libvorbis.
- Preview Raylib opzionale.
- JSON opzionale tramite nlohmann/json.
- 12 test automatici senza framework esterni.

## Build base

La configurazione predefinita non richiede dipendenze esterne:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Il build base include:

- core DSP;
- preset;
- encoder WAV;
- stub espliciti per OGG, Raylib e JSON quando le feature sono disabilitate;
- esempio e test.

## Build con Ogg Vorbis

Richiede `libogg`, `libvorbis` e `libvorbisenc`, rilevate tramite pkg-config:

```bash
cmake -S . -B build \
  -DARTCADE_SFX_ENABLE_VORBIS=ON
cmake --build build
```

L'encoder usa direttamente libvorbis: non lancia FFmpeg e non crea comandi shell.

## Build con Raylib

```bash
cmake -S . -B build \
  -DARTCADE_SFX_ENABLE_RAYLIB=ON
cmake --build build
```

Il progetto deve rendere disponibile un package CMake `raylib`.

## Build con JSON

```bash
cmake -S . -B build \
  -DARTCADE_SFX_ENABLE_JSON=ON
cmake --build build
```

Richiede il package CMake `nlohmann_json`.

## Uso minimo

```cpp
#include "artcade/sfx/presets.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "artcade/sfx/wav_encoder.hpp"

using namespace artcade::sfx;

SfxSynthesizer synth;
auto rendered = synth.render(presets::jump());
if (!rendered) {
    // rendered.error().code / rendered.error().message
    return;
}

WavEncoder wav;
auto saved = wav.encode(rendered.value(), "assets/audio/generated/jump.wav");
```

Con il supporto Vorbis abilitato:

```cpp
#include "artcade/sfx/vorbis_encoder.hpp"

VorbisEncoder encoder;
VorbisEncodeSettings settings;
settings.quality = 0.55f;

auto saved = encoder.encode(
    rendered.value(),
    "assets/audio/generated/jump.ogg",
    settings
);
```

## Feature flags

| Flag | Default | Funzione |
|---|---:|---|
| `ARTCADE_SFX_BUILD_TESTS` | ON | Compila i test |
| `ARTCADE_SFX_BUILD_EXAMPLE` | ON | Compila l'esempio |
| `ARTCADE_SFX_ENABLE_VORBIS` | OFF | Encoding Ogg Vorbis diretto |
| `ARTCADE_SFX_ENABLE_RAYLIB` | OFF | Preview tramite Raylib |
| `ARTCADE_SFX_ENABLE_JSON` | OFF | Recipe JSON tramite nlohmann/json |

## Struttura

```text
include/artcade/sfx/
  result.hpp
  types.hpp
  synthesizer.hpp
  presets.hpp
  wav_encoder.hpp
  vorbis_encoder.hpp
  raylib_preview.hpp
  recipe_json.hpp

src/
  synthesizer.cpp
  presets.cpp
  wav_encoder.cpp
  vorbis_encoder.cpp / stub
  raylib_preview.cpp / stub
  recipe_json.cpp / stub

tests/
  sfx_tests.cpp

docs/
  SPECIFICATION.md
  ARTCADE_INTEGRATION.md
```

## Formati

- WAV PCM16: disponibile senza dipendenze.
- Ogg Vorbis: disponibile con libogg/libvorbis/libvorbisenc.
- MP3: deliberatamente escluso dal core v2. Per SFX brevi OGG/WAV sono più adatti;
  un eventuale adapter MP3 deve restare separato dal sintetizzatore.

## Nota licenze

Il codice non incorpora le librerie Xiph. Quando ArtCade distribuisce una build che
linka o include libogg/libvorbis, deve aggiungere i relativi testi di licenza BSD
alla documentazione di terze parti. Vedere `third_party/README.md`.
