<#
.SYNOPSIS
    Links an Arduino-generated M5Unified sketch object into the FMP3 image.
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
    #  -Chip picks the board's default FQBN and the toolchain's name; -Fqbn
    #  overrides the former. Defaults keep this test's CoreS3 behaviour.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    [string]$Fqbn = '',
    [switch]$ReuseArduinoObjects
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($Fqbn)) {
    $Fqbn = switch ($Chip) {
        'esp32' { 'm5stack:esp32:m5stack_core' }
        default { 'm5stack:esp32:m5stack_cores3' }
    }
}
#  Only a non-default chip gets a suffix, so the esp32s3 paths stay exactly
#  what they were - other tests read these directories by name.
$chipSuffix = if ($Chip -eq 'esp32s3') { '' } else { "-$Chip" }
if ([string]::IsNullOrWhiteSpace($ArduinoBuildPath)) {
    $ArduinoBuildPath = Join-Path $M5ArduinoRoot `
        "build\arduino-phase4-m5unified$chipSuffix"
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot `
        "build\phase4-seam-s3-m5$chipSuffix"
}

$recipeScript = Join-Path $M5ArduinoRoot 'scripts\Invoke-SketchLinkRecipe.ps1'
$sketch = Join-Path $M5ArduinoRoot 'examples\M5UnifiedLink'
$nm = Join-Path $M5StackPackage `
    "tools\esp-x32\2601\bin\xtensa-$Chip-elf-nm.exe"

foreach ($required in @($ArduinoCli, $recipeScript, $sketch, $nm)) {
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
    "-FmpApplicationDirectory `"$M5ArduinoRoot\fmp_app\phase4`""
    '-FmpApplicationName phase4_freertos_app'
    #  phase4 includes freertos/FreeRTOS.h; the m5-unified profile is the
    #  one that puts a FreeRTOS shim on the include path.
    '-Profile m5-unified'
    "-Chip $Chip"
) -join ' '

if ($ReuseArduinoObjects) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $recipeScript `
        -Mode Link `
        -ArduinoBuildPath $ArduinoBuildPath `
        -ProjectName 'M5UnifiedLink.ino' `
        -M5ArduinoRoot $M5ArduinoRoot `
        -FmpBuildDirectory $FmpBuildDirectory `
        -FmpApplicationDirectory "$M5ArduinoRoot\fmp_app\phase4" `
        -FmpApplicationName phase4_freertos_app `
        -Profile m5-unified
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
        throw "Arduino M5Unified link failed (exit=$LASTEXITCODE)"
    }
}

$projectName = 'M5UnifiedLink.ino'
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
    '_kernel_tcb_PHASE4_PROBE_TASK',
    'setup()',
    'loop()',
    'toppers_arduino_task',
    'phase4_freertos_probe_task',
    'phase4_freertos_probe_failures',
    'phase4_freertos_probe_checks',
    'phase4_freertos_tick_delta',
    'phase8_queue_checks',
    'phase8_fromisr_wrapper_checks',
    'phase8_pool_checks',
    'M5',
    'phase4_m5unified_address',
    'phase4_m5unified_loop_count'
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
Write-Host 'M5Unified link passed static validation.'
Get-FileHash -Algorithm SHA256 $elf, $bin |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  Arduino sketch references the FMP3-linked global M5 object.'
Write-Host '  M5.begin() is intentionally not called in this phase.'
Write-Host '  Arduino app_main and the FreeRTOS scheduler are absent.'
Write-Host '  FreeRTOS timeout/error conversion probe is linked for hardware validation.'
