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

echo [editor 1/3] Loading MSVC environment...
call "!VSDEVCMD!" -arch=x64 >nul || ( echo [FAIL] VsDevCmd failed & exit /b 1 )
if defined VCToolsInstallDir if exist "!VCToolsInstallDir!lib\onecore\x64\oldnames.lib" set "LIB=!VCToolsInstallDir!lib\onecore\x64;!LIB!"

pushd "%ROOT%" >nul
echo [editor 2/3] Configuring (Ninja, Release)...
"%CMAKE_EXE%" -S . -B "!BUILD_DIR!" -G Ninja -Wno-dev ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM="!NINJA_EXE!" ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 ( popd >nul & echo [FAIL] configure failed. & exit /b 1 )

echo [editor 3/3] Building artcade-editor-native...
"%CMAKE_EXE%" --build "!BUILD_DIR!" --target artcade-editor-native
if errorlevel 1 ( popd >nul & echo [FAIL] build failed. & exit /b 1 )

if "!DO_TEST!"=="1" (
    echo [editor] Building + running editor core suites and sfx_synthesizer_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target editor_core_test editor_actions_shortcut_test sprite_animation_test tileset_tilemap_test generated_sfx_model_test script_asset_test script_delete_disk_test export_foundation_test script_text_ops_test script_api_catalog_test generated_sfx_editor_controller_test generated_sfx_generation_service_test sfx_synthesizer_test ui_stylesheet_tokens_test ui_markup_flex_text_test
    if errorlevel 1 ( popd >nul & echo [FAIL] test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\editor_core_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] editor_core_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\editor_actions_shortcut_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] editor_actions_shortcut_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\sprite_animation_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] sprite_animation_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\tileset_tilemap_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] tileset_tilemap_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\generated_sfx_model_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] generated_sfx_model_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\script_asset_test.exe"
    set "SCRIPT_ASSET_EC=!ERRORLEVEL!"
    echo script_asset_test exit=!SCRIPT_ASSET_EC!
    if not "!SCRIPT_ASSET_EC!"=="0" ( popd >nul & echo [FAIL] script_asset_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\script_delete_disk_test.exe"
    if not "!ERRORLEVEL!"=="0" ( popd >nul & echo [FAIL] script_delete_disk_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\export_foundation_test.exe"
    if not "!ERRORLEVEL!"=="0" ( popd >nul & echo [FAIL] export_foundation_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\script_text_ops_test.exe"
    if not "!ERRORLEVEL!"=="0" ( popd >nul & echo [FAIL] script_text_ops_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\script_api_catalog_test.exe"
    if not "!ERRORLEVEL!"=="0" ( popd >nul & echo [FAIL] script_api_catalog_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\generated_sfx_editor_controller_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] generated_sfx_editor_controller_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\generated_sfx_generation_service_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] generated_sfx_generation_service_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\sfx_synthesizer_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] sfx_synthesizer_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\ui_stylesheet_tokens_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] ui_stylesheet_tokens_test failed. & exit /b 1 )
    "!BUILD_DIR!\tests\ui_markup_flex_text_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] ui_markup_flex_text_test failed. & exit /b 1 )
    rem ADR-0027 phase 4: renders the component gallery and diffs it against the
    rem committed reference. Needs Python + Pillow, and a real GPU render — the
    rem reference is machine-specific, so regenerate it (--update) if the editor
    rem is built on different hardware or at a different DPI.
    echo [editor] Checking the component gallery against its reference...
    python "%ROOT%\scripts\check_ui_gallery.py"
    if errorlevel 1 ( popd >nul & echo [FAIL] UI gallery differs from its reference. & exit /b 1 )
    echo [editor] Building + running logic_board_editor_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target logic_board_editor_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Logic Board test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\logic_board_editor_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] logic_board_editor_test failed. & exit /b 1 )
    rem ADR-0029: drives a real RmlUi focus event through EditorUi's listener,
    rem so it links the editor UI objects rather than editor-core alone.
    echo [editor] Building + running logic_expression_focus_routing_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target logic_expression_focus_routing_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Expression focus test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\logic_expression_focus_routing_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] logic_expression_focus_routing_test failed. & exit /b 1 )
    rem ADR-0031 A2: drives real Inspector rendering and Object Variables events
    rem through EditorUi, including the section-marker and dropdown regressions.
    echo [editor] Building + running inspector_object_variables_routing_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target inspector_object_variables_routing_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Inspector Object Variables test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\inspector_object_variables_routing_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] inspector_object_variables_routing_test failed. & exit /b 1 )
    rem ADR-0034 spike: Layer dropdown arrow-key highlight, Enter-commit,
    rem Escape-close, and the hasOpenContextMenu() gap fix.
    echo [editor] Building + running inspector_layer_dropdown_keyboard_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target inspector_layer_dropdown_keyboard_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Inspector Layer Dropdown Keyboard test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\inspector_layer_dropdown_keyboard_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] inspector_layer_dropdown_keyboard_test failed. & exit /b 1 )
    rem ADR-0035: Logic Board WHEN trigger-type catalog keyboard nav + Escape gap fix.
    echo [editor] Building + running logic_board_dropdown_keyboard_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target logic_board_dropdown_keyboard_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Logic Board Dropdown Keyboard test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\logic_board_dropdown_keyboard_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] logic_board_dropdown_keyboard_test failed. & exit /b 1 )
    rem ADR-0036: Text component Font picker data plumbing.
    echo [editor] Building + running inspector_text_font_picker_test...
    "%CMAKE_EXE%" --build "!BUILD_DIR!" --target inspector_text_font_picker_test
    if errorlevel 1 ( popd >nul & echo [FAIL] Inspector Text Font Picker test build failed. & exit /b 1 )
    "!BUILD_DIR!\tests\inspector_text_font_picker_test.exe"
    if errorlevel 1 ( popd >nul & echo [FAIL] inspector_text_font_picker_test failed. & exit /b 1 )
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
