# Runtime vendor

`artcade-runtime` is the ArtCade C++ engine (`runtime-cpp`). It is **not** vendored as a copy in git.

## Local development (junction)

```powershell
cmd /c mklink /J vendor\artcade-runtime ..\ArtCade-Studio_V2\runtime-cpp
```

## Git submodule (CI / second machine)

```powershell
git submodule add <artcade-runtime-repo-url> vendor/artcade-runtime
git submodule update --init
```

CMake fails fast if `vendor/artcade-runtime/CMakeLists.txt` is missing.
