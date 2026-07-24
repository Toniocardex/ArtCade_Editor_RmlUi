@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ArtCade WASM build script.
rem - Uses Ninja + Emscripten only (no MSVC/nmake needed for the WASM target).
rem - Configures/builds runtime-cpp/build-wasm.
rem - Copies game.js/game.wasm/game.data to dist/wasm (runtime ship artifacts).

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build-wasm"
set "OUTDIR=%BUILD_DIR%\src\app"
set "EDITOR_RUNTIME=%SCRIPT_DIR%..\dist\wasm"

if "%EMSDK%"=="" set "EMSDK=%USERPROFILE%\emsdk"
if not exist "%EMSDK%\emsdk_env.bat" (
    if exist "%USERPROFILE%\DevTools\emsdk\emsdk_env.bat" set "EMSDK=%USERPROFILE%\DevTools\emsdk"
)
set "EMSCRIPTEN=%EMSDK%\upstream\emscripten"

rem Ninja: prefer PATH, fall back to the known local install.
set "NINJA_DIR=%USERPROFILE%\DevTools\ninja"
if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"
set "NINJA_DIR=%LOCALAPPDATA%\ninja"
if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"

set "CMAKE_EXE=cmake"
if exist "%USERPROFILE%\DevTools\cmake\bin\cmake.exe" set "CMAKE_EXE=%USERPROFILE%\DevTools\cmake\bin\cmake.exe"
if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"

if /I "%~1"=="--clean" (
    echo [WASM] Removing "%BUILD_DIR%"
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

set "EMCMAKE=%EMSCRIPTEN%\emcmake.bat"
if not exist "!EMCMAKE!" set "EMCMAKE=%EMSCRIPTEN%\emcmake.exe"
set "EMMAKE=%EMSCRIPTEN%\emmake.bat"
if not exist "!EMMAKE!" set "EMMAKE=%EMSCRIPTEN%\emmake.exe"
if not exist "!EMCMAKE!" (
    echo [FAIL] Emscripten not found. Expected:
    echo        !EMSCRIPTEN!\emcmake.bat or emcmake.exe
    echo        Set EMSDK to your emsdk root and retry.
    exit /b 1
)

if not exist "%EMSDK%\emsdk_env.bat" (
    echo [FAIL] emsdk_env.bat not found:
    echo        !EMSDK!\emsdk_env.bat
    exit /b 1
)

echo [WASM 1/4] Loading Emscripten environment...
set "EMSDK_QUIET=1"
call "%EMSDK%\emsdk_env.bat" >nul
if errorlevel 1 (
    echo [FAIL] Emscripten environment setup failed.
    exit /b 1
)

pushd "%SCRIPT_DIR%" >nul

rem Guard: a CMake cache from a different generator (e.g. NMake/MSVC) makes
rem reconfigure fail with "does not match the generator used previously".
rem We standardise on Ninja; wipe a stale non-Ninja cache automatically.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_GENERATOR:INTERNAL=Ninja" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo [WASM] Stale non-Ninja CMake cache detected - cleaning "%BUILD_DIR%"
        rmdir /s /q "%BUILD_DIR%"
    )
)

echo [WASM 2/4] Configuring CMake (Ninja)...
call "!EMCMAKE!" "%CMAKE_EXE%" ^
    -S . ^
    -B "%BUILD_DIR%" ^
    -G Ninja ^
    -Wno-dev ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    -DPLATFORM=Web ^
    -DARTCADE_BUILD_TESTS=OFF
if errorlevel 1 (
    popd >nul
    echo [FAIL] CMake configure failed.
    exit /b 1
)

rem Emscripten does not always track --preload-file inputs as build deps.
rem Remove generated app outputs so game.data is repacked when scripts/assets change.
if exist "%OUTDIR%\game.html" del /q "%OUTDIR%\game.html"
if exist "%OUTDIR%\game.js"   del /q "%OUTDIR%\game.js"
if exist "%OUTDIR%\game.wasm" del /q "%OUTDIR%\game.wasm"
if exist "%OUTDIR%\game.data" del /q "%OUTDIR%\game.data"

echo [WASM 3/4] Building runtime...
call "!EMMAKE!" "%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    popd >nul
    echo [FAIL] WASM build failed.
    exit /b 1
)

echo [WASM 4/4] Copying preview runtime assets...
if not exist "%OUTDIR%\game.js" (
    popd >nul
    echo [FAIL] Missing output: !OUTDIR!\game.js
    exit /b 1
)
if not exist "%OUTDIR%\game.wasm" (
    popd >nul
    echo [FAIL] Missing output: !OUTDIR!\game.wasm
    exit /b 1
)
if not exist "%EDITOR_RUNTIME%" mkdir "%EDITOR_RUNTIME%"
copy /y "%OUTDIR%\game.js" "%EDITOR_RUNTIME%\game.js" >nul
copy /y "%OUTDIR%\game.wasm" "%EDITOR_RUNTIME%\game.wasm" >nul
if exist "%OUTDIR%\game.data" (
    copy /y "%OUTDIR%\game.data" "%EDITOR_RUNTIME%\game.data" >nul
) else if exist "%EDITOR_RUNTIME%\game.data" (
    del /q "%EDITOR_RUNTIME%\game.data"
)
if errorlevel 1 (
    popd >nul
    echo [FAIL] Failed to copy runtime assets to:
    echo        !EDITOR_RUNTIME!
    exit /b 1
)

echo.
echo [OK] WASM runtime updated:
for %%F in ("%EDITOR_RUNTIME%\game.js" "%EDITOR_RUNTIME%\game.wasm") do (
    echo        %%~nxF  %%~zF bytes
)
if exist "%EDITOR_RUNTIME%\game.data" (
    for %%F in ("%EDITOR_RUNTIME%\game.data") do echo        %%~nxF  %%~zF bytes
) else (
    echo        game.data  ^(none - no preload bundle^)
)

popd >nul
exit /b 0
