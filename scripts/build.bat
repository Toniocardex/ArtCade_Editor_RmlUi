@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ArtCade native editor (RmlUi) — configure + build.
rem First configure fetches RmlUi 6.1 + FreeType (network once).
rem
rem Usage:  scripts\build.bat [--clean] [--test]

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%ROOT%\build"
set "OUTDIR=%BUILD_DIR%\src"
set "DO_CLEAN=0"
set "DO_TEST=0"
if /I "%~1"=="--clean" set "DO_CLEAN=1"
if /I "%~1"=="--test" set "DO_TEST=1"
if /I "%~2"=="--clean" set "DO_CLEAN=1"
if /I "%~2"=="--test" set "DO_TEST=1"

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

where ninja >nul 2>&1 || ( echo [FAIL] ninja not found on PATH. & exit /b 1 )

if "!DO_CLEAN!"=="1" if exist "!BUILD_DIR!" (
    echo [editor] removing "!BUILD_DIR!"
    rmdir /s /q "!BUILD_DIR!"
)

echo [editor 1/3] Loading MSVC environment...
call "!VSDEVCMD!" -arch=x64 >nul || ( echo [FAIL] VsDevCmd failed & exit /b 1 )
if defined VCToolsInstallDir if exist "!VCToolsInstallDir!lib\onecore\x64\oldnames.lib" set "LIB=!VCToolsInstallDir!lib\onecore\x64;!LIB!"

pushd "%ROOT%" >nul
echo [editor 2/3] Configuring (Ninja, Release)...
"%CMAKE_EXE%" -S . -B "!BUILD_DIR!" -G Ninja -Wno-dev ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 ( popd >nul & echo [FAIL] configure failed. & exit /b 1 )

echo [editor 3/3] Building artcade-editor-native...
"%CMAKE_EXE%" --build "!BUILD_DIR!" --target artcade-editor-native
if errorlevel 1 ( popd >nul & echo [FAIL] build failed. & exit /b 1 )

if "!DO_TEST!"=="1" (
    echo [editor] Building + running editor_core_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target editor_core_test
    if errorlevel 1 ( popd >nul & echo [FAIL] test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\editor_core_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] editor_core_test failed. & exit /b 1 )
    echo [editor] Building + running logic_board_editor_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target logic_board_editor_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Logic Board test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\logic_board_editor_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] logic_board_editor_test failed. & exit /b 1 )
)
popd >nul

echo.
if exist "!OUTDIR!\artcade-editor-native.exe" (
    echo [OK] Built: !OUTDIR!\artcade-editor-native.exe
) else (
    echo [WARN] Build reported success but exe not found in !OUTDIR!
)
exit /b 0
