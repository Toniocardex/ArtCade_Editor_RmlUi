param(
    [string]$Editor = (Join-Path $PSScriptRoot "..\build\src\artcade-editor-native.exe"),
    [ValidateRange(1, 200)]
    [int]$Iterations = 20
)

$editorPath = (Resolve-Path -LiteralPath $Editor -ErrorAction Stop).Path
for ($iteration = 1; $iteration -le $Iterations; ++$iteration) {
    $process = Start-Process -FilePath $editorPath `
        -ArgumentList "--lifecycle-smoke" -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "RmlUi lifecycle smoke failed on iteration $iteration of $Iterations"
    }
}

Write-Host "[OK] RmlUi lifecycle smoke: $Iterations/$Iterations clean runs"
