@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ADR-0019: copy Release game.exe + runtime-build-info into editor export templates.
rem Does not invent assetKeyId — reads sidecar produced by the runtime build.

set "SCRIPT_DIR=%~dp0"
set "EDITOR_ROOT=%SCRIPT_DIR%.."
set "RUNTIME=%EDITOR_ROOT%\vendor\artcade-runtime"
set "GAME_EXE=%RUNTIME%\build-native\src\app\game.exe"
set "BUILD_INFO=%RUNTIME%\build-native\src\app\runtime-build-info.json"
set "OUT_DIR=%EDITOR_ROOT%\src\editor-native\resources\export-templates\windows-x64"

if not exist "%GAME_EXE%" (
    echo [FAIL] Missing game.exe:
    echo        %GAME_EXE%
    echo        Build the runtime first: vendor\artcade-runtime\build_native.bat
    exit /b 1
)
if not exist "%BUILD_INFO%" (
    echo [FAIL] Missing runtime-build-info.json beside game.exe:
    echo        %BUILD_INFO%
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

copy /y "%GAME_EXE%" "%OUT_DIR%\game.exe" >nul
if errorlevel 1 (
    echo [FAIL] Could not copy game.exe
    exit /b 1
)

set "PY=python"
where python >nul 2>&1 || set "PY=python3"

"%PY%" "%SCRIPT_DIR%refresh-export-templates.py" ^
    --game-exe "%OUT_DIR%\game.exe" ^
    --build-info "%BUILD_INFO%" ^
    --template-json "%OUT_DIR%\runtime-template.json"
if errorlevel 1 (
    echo [FAIL] Failed to refresh runtime-template.json
    exit /b 1
)

echo [OK] Export template refreshed:
echo      %OUT_DIR%\game.exe
echo      %OUT_DIR%\runtime-template.json
exit /b 0
