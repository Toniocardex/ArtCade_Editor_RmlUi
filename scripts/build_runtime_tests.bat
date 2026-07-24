@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ArtCade vendored runtime - configure + build + run the runtime test suite.
rem Uses the committed copy under vendor\artcade-runtime with
rem ARTCADE_BUILD_TESTS=ON; the build dir (vendor\artcade-runtime\build-tests)
rem is gitignored (vendor/artcade-runtime/build*/).
rem
rem Usage:  scripts\build_runtime_tests.bat [--clean]

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
set "SRC_DIR=%ROOT%\vendor\artcade-runtime"
set "BUILD_DIR=%SRC_DIR%\build-tests"
set "DO_CLEAN=0"
if /I "%~1"=="--clean" set "DO_CLEAN=1"

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
if not exist "!VSDEVCMD!" set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
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
    echo [runtime-tests] removing "!BUILD_DIR!"
    rmdir /s /q "!BUILD_DIR!"
)

echo [runtime-tests 1/4] Loading MSVC environment...
call "!VSDEVCMD!" -arch=x64 >nul || ( echo [FAIL] VsDevCmd failed & exit /b 1 )
if defined VCToolsInstallDir if exist "!VCToolsInstallDir!lib\onecore\x64\oldnames.lib" set "LIB=!VCToolsInstallDir!lib\onecore\x64;!LIB!"

echo [runtime-tests 2/4] Configuring (Ninja, Release, ARTCADE_BUILD_TESTS=ON)...
"%CMAKE_EXE%" -S "%SRC_DIR%" -B "!BUILD_DIR!" -G Ninja -Wno-dev ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DARTCADE_BUILD_TESTS=ON ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 ( echo [FAIL] configure failed. & exit /b 1 )

echo [runtime-tests 3/4] Building...
"%CMAKE_EXE%" --build "!BUILD_DIR!"
if errorlevel 1 ( echo [FAIL] build failed. & exit /b 1 )

echo [runtime-tests 4/4] Running ctest...
pushd "!BUILD_DIR!" >nul
ctest --output-on-failure
set "CTEST_EC=!ERRORLEVEL!"
popd >nul
if not "!CTEST_EC!"=="0" ( echo [FAIL] runtime tests failed. & exit /b 1 )

echo.
echo [OK] Runtime test suite green in !BUILD_DIR!
exit /b 0
