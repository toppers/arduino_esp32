<#
.SYNOPSIS
    Builds and statically validates the dual-core Arduino/FMP3 image.
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
    $ArduinoBuildPath = Join-Path $M5ArduinoRoot 'build\arduino-phase6-smp'
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot 'build\phase6-seam-s3-m5'
}

$recipeScript = Join-Path $M5ArduinoRoot 'scripts\Invoke-SketchLinkRecipe.ps1'
$sketch = Join-Path $M5ArduinoRoot 'examples\DualCore'
$applicationDirectory = Join-Path $M5ArduinoRoot 'fmp_app\phase6'
$nm = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin\xtensa-esp32s3-elf-nm.exe'

foreach ($required in @($ArduinoCli, $recipeScript, $sketch,
        $applicationDirectory, $nm)) {
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
    '-FmpApplicationName phase6_smp_selftest'
    '-ProcessorCount 2'
) -join ' '

if ($ReuseArduinoObjects) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $recipeScript `
        -Mode Link `
        -ArduinoBuildPath $ArduinoBuildPath `
        -ProjectName 'DualCore.ino' `
        -M5ArduinoRoot $M5ArduinoRoot `
        -FmpBuildDirectory $FmpBuildDirectory `
        -FmpApplicationDirectory $applicationDirectory `
        -FmpApplicationName phase6_smp_selftest `
        -ProcessorCount 2
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
        throw "Arduino SMP build failed (exit=$LASTEXITCODE)"
    }
}

$projectName = 'DualCore.ino'
$elf = Join-Path $ArduinoBuildPath "$projectName.elf"
$bin = Join-Path $ArduinoBuildPath "$projectName.bin"
$mergedBin = Join-Path $ArduinoBuildPath "$projectName.merged.bin"
foreach ($required in @($elf, $bin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Expected artifact was not found: $required"
    }
}

$defined = @(& $nm -C --defined-only $elf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while inspecting the SMP ELF.'
}

$requiredSymbols = @(
    '_start',
    '_kernel_start_dispatch',
    'setup()',
    'loop()',
    'toppers_arduino_task',
    'phase6_prc2_task',
    'phase6_monitor_task',
    '_kernel_tcb_ARDUINO_TASK',
    '_kernel_tcb_PHASE6_PRC2_TASK',
    '_kernel_tcb_PHASE6_MONITOR_TASK',
    'phase6_arduino_processor',
    'phase6_prc2_processor',
    'phase6_monitor_pass'
)
foreach ($requiredSymbol in $requiredSymbols) {
    if (-not ($defined | Select-String -SimpleMatch " $requiredSymbol")) {
        throw "Required SMP symbol was not found: $requiredSymbol"
    }
}

foreach ($forbiddenSymbol in @('app_main', 'vTaskStartScheduler', 'loopTask(void*)')) {
    if ($defined | Select-String -SimpleMatch " $forbiddenSymbol") {
        throw "Arduino/FreeRTOS symbol was unexpectedly linked: $forbiddenSymbol"
    }
}

$cmakeCache = Join-Path $FmpBuildDirectory 'CMakeCache.txt'
$kernelConfiguration = Join-Path $FmpBuildDirectory 'generated\kernel_cfg.c'
foreach ($required in @($cmakeCache, $kernelConfiguration)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Generated build metadata was not found: $required"
    }
}
if (-not (Select-String -LiteralPath $cmakeCache -SimpleMatch `
        'A1_M5_PRC_NUM:STRING=2')) {
    throw 'The M5 seam was not configured for two processors.'
}
foreach ($task in @(
        '(TASK)(toppers_arduino_task)',
        '(TASK)(phase6_prc2_task)',
        '(TASK)(phase6_monitor_task)')) {
    if (-not (Select-String -LiteralPath $kernelConfiguration -SimpleMatch $task)) {
        throw "Generated FMP3 configuration is missing: $task"
    }
}
$affinityPatterns = @{
    'Arduino task PRC1 affinity' =
        '\(TASK\)\(toppers_arduino_task\).*\}, 1, 0x1 \}'
    'monitor task PRC1 affinity' =
        '\(TASK\)\(phase6_monitor_task\).*\}, 1, 0x1 \}'
    'worker task PRC2 affinity' =
        '\(TASK\)\(phase6_prc2_task\).*\}, 2, 0x2 \}'
}
foreach ($entry in $affinityPatterns.GetEnumerator()) {
    if (-not (Select-String -LiteralPath $kernelConfiguration `
            -Pattern $entry.Value)) {
        throw "Generated FMP3 configuration has wrong $($entry.Key)."
    }
}
$hrtInitializers = @(
    Select-String -LiteralPath $kernelConfiguration `
        -SimpleMatch '(INIRTN)(_kernel_target_hrt_initialize)'
)
if ($hrtInitializers.Count -ne 2) {
    throw "Expected two per-core HRT initializers, found $($hrtInitializers.Count)."
}
$ninjaFile = Join-Path $FmpBuildDirectory 'build.ninja'
if (-not (Select-String -LiteralPath $ninjaFile -SimpleMatch `
        'chip_ipi.c.obj')) {
    throw 'The ESP32-S3 inter-processor interrupt object is not in the build.'
}

if (-not $ReuseArduinoObjects) {
    if (-not (Test-Path -LiteralPath $mergedBin)) {
        throw "Expected merged image was not found: $mergedBin"
    }
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
        throw 'The SMP application is not intact at merged offset 0x10000.'
    }
}

Write-Host ''
Write-Host 'SMP image passed static validation.'
Get-FileHash -Algorithm SHA256 $elf, $bin |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  FMP3 is configured with TNUM_PRCID=2.'
Write-Host '  Arduino/monitor tasks are assigned to PRC1; worker is assigned to PRC2.'
Write-Host '  The PRC2 worker has no Arduino or M5 object dependency.'
Write-Host '  Arduino app_main and the FreeRTOS scheduler are absent.'
