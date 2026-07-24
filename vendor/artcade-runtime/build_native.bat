@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ArtCade native (Windows) build script.
rem - Uses Ninja + MSVC via VsDevCmd (same pattern as build_wasm.bat + Emscripten).
rem - Configures/builds runtime-cpp/build-native.
rem
rem Usage:
rem   build_native.bat [--clean] [--no-test] [--config Debug^|Release]

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build-native"
set "OUTDIR=%BUILD_DIR%\src\app"

set "CONFIG=Release"
set "RUN_TESTS=1"
set "DO_CLEAN=0"
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--clean" (
    set "DO_CLEAN=1"
    shift
    goto parse_args
)
if /I "%~1"=="--no-test" (
    set "RUN_TESTS=0"
    shift
    goto parse_args
)
if /I "%~1"=="--config" (
    set "CONFIG=%~2"
    shift
    shift
    goto parse_args
)
echo [FAIL] Unknown argument: %~1
echo        Usage: build_native.bat [--clean] [--no-test] [--config Debug^|Release]
exit /b 1

:args_done

set "NINJA_DIR=%USERPROFILE%\DevTools\ninja"
if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"
set "NINJA_DIR=%LOCALAPPDATA%\ninja"
if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"

set "CMAKE_EXE=cmake"
if exist "%USERPROFILE%\DevTools\cmake\bin\cmake.exe" set "CMAKE_EXE=%USERPROFILE%\DevTools\cmake\bin\cmake.exe"
if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"

if defined ARTCADE_VSDEVCMD (
    set "VSDEVCMD=!ARTCADE_VSDEVCMD!"
) else (
    set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
)
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VSDEVCMD=%%I\Common7\Tools\VsDevCmd.bat"
        )
    )
)

where ninja >nul 2>&1
if errorlevel 1 (
    echo [FAIL] ninja not found. Install Ninja or add it to PATH.
    echo        Expected fallback: !NINJA_DIR!\ninja.exe
    exit /b 1
)

if not exist "!VSDEVCMD!" (
    echo [FAIL] Visual Studio DevCmd not found.
    echo        Set ARTCADE_VSDEVCMD to VsDevCmd.bat and retry.
    exit /b 1
)

if "!DO_CLEAN!"=="1" (
    echo [Native] Removing "!BUILD_DIR!"
    if exist "!BUILD_DIR!" rmdir /s /q "!BUILD_DIR!"
)

echo [Native 1/4] Loading MSVC environment...
call "!VSDEVCMD!" -arch=x64 >nul
if errorlevel 1 (
    echo [FAIL] VsDevCmd failed: !VSDEVCMD!
    exit /b 1
)

rem ── Workaround: VS BuildTools 14.50 is missing several x64 desktop CRT libs
rem    (oldnames.lib, vcruntime.lib, msvcprt.lib, legacy_stdio_definitions.lib).
rem    They exist in onecore\x64 with compatible ABI for standard game-engine code.
rem    Prepend onecore\x64 to LIB so the linker finds them automatically.
if defined VCToolsInstallDir if exist "!VCToolsInstallDir!lib\onecore\x64\oldnames.lib" (
    set "LIB=!VCToolsInstallDir!lib\onecore\x64;!LIB!"
)

pushd "%SCRIPT_DIR%" >nul

if exist "!BUILD_DIR!\CMakeCache.txt" (
    findstr /C:"CMAKE_GENERATOR:INTERNAL=Ninja" "!BUILD_DIR!\CMakeCache.txt" >nul 2>&1
    if errorlevel 1 (
        echo [Native] Stale non-Ninja CMake cache detected - cleaning "!BUILD_DIR!"
        rmdir /s /q "!BUILD_DIR!"
    )
)

echo [Native 2/4] Configuring CMake (Ninja, !CONFIG!)...
"%CMAKE_EXE%" -S . -B "!BUILD_DIR!" -G Ninja -Wno-dev ^
    -DCMAKE_BUILD_TYPE=!CONFIG! ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    -DARTCADE_BUILD_TESTS=ON
if errorlevel 1 (
    popd >nul
    echo [FAIL] CMake configure failed.
    exit /b 1
)

echo [Native 3/4] Building runtime...
"%CMAKE_EXE%" --build "!BUILD_DIR!"
if errorlevel 1 (
    popd >nul
    echo [FAIL] Native build failed.
    exit /b 1
)

if "!RUN_TESTS!"=="1" (
    echo [Native 4/4] Running tests...
    ctest --test-dir "!BUILD_DIR!" --output-on-failure -C !CONFIG!
    if errorlevel 1 (
        popd >nul
        echo [FAIL] Tests failed.
        exit /b 1
    )
) else (
    echo [Native 4/4] Tests skipped.
)

popd >nul
echo.
echo [OK] Native runtime updated:
if exist "!OUTDIR!\game.exe" (
    for %%F in ("!OUTDIR!\game.exe") do echo        game.exe  %%~zF bytes
) else (
    echo        game.exe  not found in !OUTDIR!
)
exit /b 0
