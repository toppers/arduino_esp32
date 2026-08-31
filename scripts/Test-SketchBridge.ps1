<#
.SYNOPSIS
    Builds and inspects the Arduino setup()/loop() to FMP3 task bridge.
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
    [string]$Fqbn = 'm5stack:esp32:m5stack_cores3'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($ArduinoBuildPath)) {
    $ArduinoBuildPath = Join-Path $M5ArduinoRoot 'build\arduino-phase3-bridge'
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot 'build\phase3-seam-s3-m5'
}

$recipeScript = Join-Path $M5ArduinoRoot 'scripts\Invoke-SketchLinkRecipe.ps1'
$sketch = Join-Path $M5ArduinoRoot 'examples\Fmp3Minimal'
$nm = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin\xtensa-esp32s3-elf-nm.exe'

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
) -join ' '
$linkPattern = "$commonRecipeArguments -Mode Link"
$objcopyPattern = "$commonRecipeArguments -Mode Objcopy"

& $ArduinoCli compile `
    --fqbn $Fqbn `
    --library $M5ArduinoRoot `
    --build-path $ArduinoBuildPath `
    --build-property "recipe.c.combine.pattern=$linkPattern" `
    --build-property "recipe.objcopy.bin.pattern=$objcopyPattern" `
    $sketch
if ($LASTEXITCODE -ne 0) {
    throw "Arduino bridge compile failed (exit=$LASTEXITCODE)"
}

$projectName = 'Fmp3Minimal.ino'
$elf = Join-Path $ArduinoBuildPath "$projectName.elf"
$bin = Join-Path $ArduinoBuildPath "$projectName.bin"
$mergedBin = Join-Path $ArduinoBuildPath "$projectName.merged.bin"
foreach ($required in @($elf, $bin, $mergedBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Expected artifact was not found: $required"
    }
}

$defined = @(& $nm -C --defined-only $elf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while inspecting the bridge ELF.'
}

$requiredSymbols = @(
    '_start',
    '_kernel_start_dispatch',
    'setup()',
    'loop()',
    'toppers_arduino_task',
    '_kernel_tcb_ARDUINO_TASK',
    'toppers_arduino_setup_calls',
    'toppers_arduino_loop_calls',
    'phase3_sketch_setup_count',
    'phase3_sketch_loop_count'
)
foreach ($requiredSymbol in $requiredSymbols) {
    if (-not ($defined | Select-String -SimpleMatch " $requiredSymbol")) {
        throw "Required bridge symbol was not found: $requiredSymbol"
    }
}

$kernelConfiguration = Join-Path $FmpBuildDirectory 'generated\kernel_cfg.c'
if (-not (Test-Path -LiteralPath $kernelConfiguration)) {
    throw "Generated FMP3 configuration was not found: $kernelConfiguration"
}
if (-not (Select-String -LiteralPath $kernelConfiguration -SimpleMatch `
        '{ (TA_ACT | TA_FPU), (EXINF)(0), (TASK)(toppers_arduino_task)')) {
    throw 'ARDUINO_TASK is not configured as an auto-start FPU task.'
}

foreach ($forbiddenSymbol in @('app_main', 'vTaskStartScheduler', 'loopTask(void*)')) {
    if ($defined | Select-String -SimpleMatch " $forbiddenSymbol") {
        throw "Arduino/FreeRTOS symbol was unexpectedly linked: $forbiddenSymbol"
    }
}

$applicationBytes = [System.IO.File]::ReadAllBytes($bin)
$mergedStream = [System.IO.File]::OpenRead($mergedBin)
try {
    [void]$mergedStream.Seek(0x10000, [System.IO.SeekOrigin]::Begin)
    $mergedApplication = [byte[]]::new($applicationBytes.Length)
    $readLength = $mergedStream.Read($mergedApplication, 0, $mergedApplication.Length)
}
finally {
    $mergedStream.Dispose()
}
if ($readLength -ne $applicationBytes.Length -or
    -not [System.Collections.StructuralComparisons]::StructuralEqualityComparer.Equals(
        $applicationBytes, $mergedApplication)) {
    throw 'The bridge application is not intact at merged offset 0x10000.'
}

Write-Host ''
Write-Host 'Sketch bridge passed static validation.'
Get-FileHash -Algorithm SHA256 $elf, $bin |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  setup()/loop() and FMP3 task bridge symbols are linked.'
Write-Host '  Arduino app_main and the FreeRTOS scheduler are absent.'
Write-Host '  FMP3 application matches merged offset 0x10000.'
