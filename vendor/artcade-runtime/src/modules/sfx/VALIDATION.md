# Validazione del pacchetto

Validazione eseguita sul core predefinito con GCC 14.2, CMake Release e warning:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wshadow
```

Risultato:

```text
12/12 test superati
```

È stato inoltre eseguito un build Debug con:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

I test sono passati senza errori AddressSanitizer o UndefinedBehaviorSanitizer.

Gli adapter opzionali Ogg Vorbis, Raylib e nlohmann/json sono inclusi nel sorgente,
ma non sono stati compilati in questo ambiente perché le rispettive dipendenze di
sviluppo non erano installate. Quando vengono abilitati nel repository ArtCade,
vanno inclusi nel CI con le versioni effettivamente usate dal progetto.
