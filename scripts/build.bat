@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ArtCade native editor (RmlUi) — configure + build.
rem First configure fetches RmlUi 6.1 + FreeType (network once).
rem
rem Usage:  scripts\build.bat [--clean] [--test] [--fixture-demo]
rem Flags may appear in any order.

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT%\build"
set "OUTDIR=%BUILD_DIR%\src"
set "DO_CLEAN=0"
set "DO_TEST=0"
set "DO_FIXTURE_DEMO=0"
for %%A in (%*) do (
    if /I "%%~A"=="--clean" set "DO_CLEAN=1"
    if /I "%%~A"=="--test" set "DO_TEST=1"
    if /I "%%~A"=="--fixture-demo" set "DO_FIXTURE_DEMO=1"
)

set "NINJA_DIR=%USERPROFILE%\DevTools\ninja"
set "NINJA_EXE="
if exist "%NINJA_DIR%\ninja.exe" (
    set "NINJA_EXE=%NINJA_DIR%\ninja.exe"
    set "PATH=%NINJA_DIR%;%PATH%"
)
set "NINJA_DIR=%LOCALAPPDATA%\ninja"
if not defined NINJA_EXE if exist "%NINJA_DIR%\ninja.exe" (
    set "NINJA_EXE=%NINJA_DIR%\ninja.exe"
    set "PATH=%NINJA_DIR%;%PATH%"
)

set "CMAKE_EXE=cmake"
if exist "%USERPROFILE%\DevTools\cmake\bin\cmake.exe" set "CMAKE_EXE=%USERPROFILE%\DevTools\cmake\bin\cmake.exe"
if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"

if defined ARTCADE_VSDEVCMD (
    set "VSDEVCMD=!ARTCADE_VSDEVCMD!"
) else (
    set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
)
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDEVCMD=%%I\Common7\Tools\VsDevCmd.bat"
)
if not exist "!VSDEVCMD!" (
    echo [FAIL] Visual Studio DevCmd not found. Set ARTCADE_VSDEVCMD and retry.
    exit /b 1
)

if not defined NINJA_EXE (
    for %%I in (ninja.exe) do set "NINJA_EXE=%%~$PATH:I"
)
if not defined NINJA_EXE ( echo [FAIL] ninja.exe not found. Install Ninja or set it on PATH. & exit /b 1 )

if "!DO_CLEAN!"=="1" if exist "!BUILD_DIR!" (
    echo [editor] removing "!BUILD_DIR!"
    rmdir /s /q "!BUILD_DIR!"
)

echo [editor 1/4] Loading MSVC environment...
call "!VSDEVCMD!" -arch=x64 >nul || ( echo [FAIL] VsDevCmd failed & exit /b 1 )
if defined VCToolsInstallDir if exist "!VCToolsInstallDir!lib\onecore\x64\oldnames.lib" set "LIB=!VCToolsInstallDir!lib\onecore\x64;!LIB!"

pushd "%ROOT%" >nul
set "RUNTIME_BUILD_ARGS=--no-test --config Release"
if "!DO_CLEAN!"=="1" set "RUNTIME_BUILD_ARGS=--clean --no-test --config Release"

echo [export 2/4] Building the native Windows player...
call "!ROOT!\vendor\artcade-runtime\build_native.bat" !RUNTIME_BUILD_ARGS!
if errorlevel 1 ( popd >nul & echo [FAIL] native Windows player build failed. & exit /b 1 )

echo [editor 3/4] Configuring (Ninja, Release)...
"%CMAKE_EXE%" -S . -B "!BUILD_DIR!" -G Ninja -Wno-dev ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM="!NINJA_EXE!" ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 ( popd >nul & echo [FAIL] configure failed. & exit /b 1 )

echo [export] Synchronizing the Windows export template...
call "!ROOT!\scripts\refresh-export-templates.bat"
if errorlevel 1 ( popd >nul & echo [FAIL] export template refresh failed. & exit /b 1 )

echo [editor 4/4] Building artcade-editor-native...
"%CMAKE_EXE%" --build "!BUILD_DIR!" --target artcade-editor-native
if errorlevel 1 ( popd >nul & echo [FAIL] build failed. & exit /b 1 )

if "!DO_TEST!"=="1" (
    echo [editor] Building every CTest-registered executable...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target artcade-editor-tests
    if errorlevel 1 ( popd >nul & echo [FAIL] test build failed. & exit /b 1 )
    for %%I in ("%CMAKE_EXE%") do set "CTEST_EXE=%%~dpIctest.exe"
    if not exist "!CTEST_EXE!" set "CTEST_EXE=ctest"
    echo [editor] Running CTest gate...
    "!CTEST_EXE!" --test-dir "!BUILD_DIR!" --output-on-failure
    if errorlevel 1 ( popd >nul & echo [FAIL] CTest gate failed. & exit /b 1 )
    rem ADR-0027 phase 4: renders the component gallery and diffs it against the
    rem committed reference. Needs Python + Pillow, and a real GPU render — the
    rem reference is machine-specific, so regenerate it (--update) if the editor
    rem is built on different hardware or at a different DPI.
    echo [editor] Checking the component gallery against its reference...
    python "%ROOT%\scripts\check_ui_gallery.py"
    if errorlevel 1 ( popd >nul & echo [FAIL] UI gallery differs from its reference. & exit /b 1 )
)

if "!DO_FIXTURE_DEMO!"=="1" (
    if not exist "!OUTDIR!\artcade-editor-native.exe" (
        popd >nul
        echo [FAIL] Fixture Demo Suite: editor exe missing after build.
        exit /b 1
    )
    echo [editor] Running Fixture Demo Suite...
    python "%ROOT%\scripts\run_fixture_demo.py"
    if errorlevel 1 (
        popd >nul
        echo [FAIL] Fixture Demo Suite failed.
        exit /b 1
    )
)
popd >nul

echo.
if exist "!OUTDIR!\artcade-editor-native.exe" (
    echo [OK] Built: !OUTDIR!\artcade-editor-native.exe
) else (
    echo [WARN] Build reported success but exe not found in !OUTDIR!
)
exit /b 0
