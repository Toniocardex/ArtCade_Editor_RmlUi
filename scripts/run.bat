@echo off
setlocal EnableExtensions

rem Launch artcade-editor-native from its build output (resources/ copied next to exe).
rem Usage:  scripts\run.bat [args...]

set "SCRIPT_DIR=%~dp0"
set "OUTDIR=%SCRIPT_DIR%..\build\src"
set "EXE=%OUTDIR%\artcade-editor-native.exe"

if not exist "%EXE%" (
    echo [FAIL] %EXE% not found. Run scripts\build.bat first.
    exit /b 1
)

pushd "%OUTDIR%" >nul
"%EXE%" %*
set "RC=%errorlevel%"
popd >nul
exit /b %RC%
