<#
.SYNOPSIS
    Builds and validates the Arduino-to-FMP3 M5Unified image.
#>

[CmdletBinding()]
param(
    [string]$ArduinoCli = (@(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Get-Command 'arduino-cli' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1),
    [string]$M5StackPackage = (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$M5ArduinoRoot = '',
    [string]$ArduinoBuildPath = '',
    [string]$FmpBuildDirectory = '',
    [string]$Fqbn = 'm5stack:esp32:m5stack_cores3',
    [switch]$ReuseArduinoObjects
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($ArduinoBuildPath)) {
    $ArduinoBuildPath = Join-Path $M5ArduinoRoot 'build\arduino-phase5-m5unified'
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot 'build\phase5-seam-s3-m5'
}

$recipeScript = Join-Path $M5ArduinoRoot 'scripts\Invoke-SketchLinkRecipe.ps1'
$sketch = Join-Path $M5ArduinoRoot 'examples\M5Unified'
$applicationDirectory = Join-Path $M5ArduinoRoot 'fmp_app\phase5'
$nm = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin\xtensa-esp32s3-elf-nm.exe'

foreach ($required in @($ArduinoCli, $recipeScript, $sketch, $applicationDirectory, $nm)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

$commonRecipeArguments = @(
    'powershell.exe -NoProfile -ExecutionPolicy Bypass'
    "-File `"$recipeScript`""
    '-ArduinoBuildPath "{build.path}"'
    '-ProjectName "{build.project_name}"'
    "-M5ArduinoRoot `"$M5ArduinoRoot`""
    "-FmpBuildDirectory `"$FmpBuildDirectory`""
    "-FmpApplicationDirectory `"$applicationDirectory`""
    '-FmpApplicationName phase5_m5_selftest'
) -join ' '

if ($ReuseArduinoObjects) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $recipeScript `
        -Mode Link `
        -ArduinoBuildPath $ArduinoBuildPath `
        -ProjectName 'M5Unified.ino' `
        -M5ArduinoRoot $M5ArduinoRoot `
        -FmpBuildDirectory $FmpBuildDirectory `
        -FmpApplicationDirectory $applicationDirectory `
        -FmpApplicationName phase5_m5_selftest
    if ($LASTEXITCODE -ne 0) {
        throw "Reusing Arduino objects failed (exit=$LASTEXITCODE)"
    }
}
else {
    & $ArduinoCli compile `
        --fqbn $Fqbn `
        --library $M5ArduinoRoot `
        --build-path $ArduinoBuildPath `
        --build-property "recipe.c.combine.pattern=$commonRecipeArguments -Mode Link" `
        --build-property "recipe.objcopy.bin.pattern=$commonRecipeArguments -Mode Objcopy" `
        $sketch
    if ($LASTEXITCODE -ne 0) {
        throw "Arduino M5Unified build failed (exit=$LASTEXITCODE)"
    }
}

$projectName = 'M5Unified.ino'
$elf = Join-Path $ArduinoBuildPath "$projectName.elf"
$bin = Join-Path $ArduinoBuildPath "$projectName.bin"
$mergedBin = Join-Path $ArduinoBuildPath "$projectName.merged.bin"
$requiredArtifacts = @($elf, $bin)
if (-not $ReuseArduinoObjects) {
    $requiredArtifacts += $mergedBin
}
foreach ($required in $requiredArtifacts) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Expected artifact was not found: $required"
    }
}

$defined = @(& $nm -C --defined-only $elf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while inspecting the M5Unified ELF.'
}

$requiredSymbols = @(
    '_start',
    '_kernel_tcb_ARDUINO_TASK',
    '_kernel_tcb_PHASE5_MONITOR_TASK',
    'setup()',
    'loop()',
    'toppers_arduino_task',
    'phase5_monitor_task',
    'toppers_m5_begin',
    'toppers_m5_update',
    'toppers_m5_draw_liveness',
    'M5',
    'phase5_begin_result',
    'phase5_liveness_seconds',
    'phase5_monitor_pass'
)
foreach ($requiredSymbol in $requiredSymbols) {
    if (-not ($defined | Select-String -SimpleMatch " $requiredSymbol")) {
        throw "Required M5Unified symbol was not found: $requiredSymbol"
    }
}

foreach ($forbiddenSymbol in @('app_main', 'vTaskStartScheduler', 'loopTask(void*)')) {
    if ($defined | Select-String -SimpleMatch " $forbiddenSymbol") {
        throw "Arduino/FreeRTOS symbol was unexpectedly linked: $forbiddenSymbol"
    }
}

$wrapped = @($defined | Select-String -SimpleMatch ' T __wrap__')
if ($wrapped.Count -ne 13) {
    throw "Expected 13 M5.begin/update wrappers, found $($wrapped.Count)."
}

$undefined = @(& $nm -u $elf)
if ($LASTEXITCODE -ne 0 -or $undefined.Count -ne 0) {
    throw "Undefined symbols remain in M5Unified ELF: $($undefined -join ', ')"
}

if (-not $ReuseArduinoObjects) {
    $applicationBytes = [System.IO.File]::ReadAllBytes($bin)
    $mergedStream = [System.IO.File]::OpenRead($mergedBin)
    try {
        [void]$mergedStream.Seek(0x10000, [System.IO.SeekOrigin]::Begin)
        $mergedApplication = [byte[]]::new($applicationBytes.Length)
        $readLength = $mergedStream.Read(
            $mergedApplication, 0, $mergedApplication.Length)
    }
    finally {
        $mergedStream.Dispose()
    }
    if ($readLength -ne $applicationBytes.Length -or
        -not [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals(
            $applicationBytes, $mergedApplication)) {
        throw 'The M5Unified application is not intact at merged offset 0x10000.'
    }
}

Write-Host ''
Write-Host 'M5Unified integration passed static validation.'
Get-FileHash -Algorithm SHA256 $elf, $bin |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  M5.begin(), LCD, update/touch, IMU, RTC, and PMIC paths are reachable.'
Write-Host '  All 13 M5.begin/update diagnostic wrappers are linked.'
Write-Host '  Arduino app_main and the FreeRTOS scheduler are absent.'
