<#
.SYNOPSIS
    Proves that Arduino CLI can package an FMP3 ELF without linking FreeRTOS.
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
    #  The standalone application depends on neither Arduino nor M5Unified,
    #  so this test is chip-agnostic apart from the names.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    [string]$Fqbn = ''
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
$chipSuffix = if ($Chip -eq 'esp32s3') { '' } else { "-$Chip" }
if ([string]::IsNullOrWhiteSpace($ArduinoBuildPath)) {
    $ArduinoBuildPath = Join-Path $M5ArduinoRoot `
        "build\arduino-phase2-recipe$chipSuffix"
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot `
        "build\baseline-seam-s3-m5$chipSuffix"
}

$recipeScript = Join-Path $M5ArduinoRoot 'scripts\Invoke-FmpImageRecipe.ps1'
$sketch = Join-Path $M5ArduinoRoot 'examples\LibraryInfo'
$nm = Join-Path $M5StackPackage `
    "tools\esp-x32\2601\bin\xtensa-$Chip-elf-nm.exe"

foreach ($required in @($ArduinoCli, $recipeScript, $sketch, $nm)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

$linkPattern = @(
    'powershell.exe -NoProfile -ExecutionPolicy Bypass'
    "-File `"$recipeScript`""
    '-Mode Link'
    '-ArduinoBuildPath "{build.path}"'
    '-ProjectName "{build.project_name}"'
    "-M5ArduinoRoot `"$M5ArduinoRoot`""
    "-FmpBuildDirectory `"$FmpBuildDirectory`""
    #  The one application that references no Arduino object at all.
    #
    #  This test publishes the FMP3 image WITHOUT linking the sketch, which is
    #  the whole property it exists to assert - Test-SketchBridge.ps1 already
    #  checks everything else here (the published ELF/BIN equalling the FMP3
    #  ones, the application surviving at merged offset 0x10000, the FMP3
    #  symbols present and the Arduino/FreeRTOS ones absent), and it checks
    #  them with the sketch linked. Take the sketch out of the link and this
    #  test says something no other one does; leave it in and it is a
    #  duplicate.
    #
    #  It used to name phase5_m5_selftest, which cannot link that way: every
    #  other application's cfg creates ARDUINO_TASK, whose body
    #  toppers_arduino_task lives in src/bridge/ArduinoSketchBridge.cpp, an
    #  object the Arduino builder produces. So this stopped at
    #      undefined reference to `toppers_arduino_task'
    #  fmp_app/standalone exists for this: an application with its own task
    #  and no reference to Arduino.
    "-ApplicationDirectory `"$M5ArduinoRoot\fmp_app\standalone`""
    '-ApplicationName standalone_app'
    #  minimal, not the recipe's m5-unified default: the application depends on
    #  neither Arduino nor M5Unified, and saying so here keeps the claim narrow
    #  (and the build far shorter than compiling M5GFX for nothing).
    '-Profile minimal'
    "-Chip $Chip"
) -join ' '

$objcopyPattern = @(
    'powershell.exe -NoProfile -ExecutionPolicy Bypass'
    "-File `"$recipeScript`""
    '-Mode Objcopy'
    '-ArduinoBuildPath "{build.path}"'
    '-ProjectName "{build.project_name}"'
    "-M5ArduinoRoot `"$M5ArduinoRoot`""
    "-FmpBuildDirectory `"$FmpBuildDirectory`""
) -join ' '

& $ArduinoCli compile `
    --fqbn $Fqbn `
    --library $M5ArduinoRoot `
    --build-path $ArduinoBuildPath `
    --build-property "recipe.c.combine.pattern=$linkPattern" `
    --build-property "recipe.objcopy.bin.pattern=$objcopyPattern" `
    $sketch
if ($LASTEXITCODE -ne 0) {
    throw "Arduino recipe compile failed (exit=$LASTEXITCODE)"
}

$projectName = 'LibraryInfo.ino'
$arduinoElf = Join-Path $ArduinoBuildPath "$projectName.elf"
$arduinoBin = Join-Path $ArduinoBuildPath "$projectName.bin"
$mergedBin = Join-Path $ArduinoBuildPath "$projectName.merged.bin"
$fmpElf = Join-Path $FmpBuildDirectory 'xip\fmp_xip.elf'
$fmpBin = Join-Path $FmpBuildDirectory 'xip\app_xip.bin'

foreach ($required in @($arduinoElf, $arduinoBin, $mergedBin, $fmpElf, $fmpBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Expected artifact was not found: $required"
    }
}

function Assert-SameHash {
    param(
        [Parameter(Mandatory)]
        [string]$Expected,
        [Parameter(Mandatory)]
        [string]$Actual,
        [Parameter(Mandatory)]
        [string]$Label
    )

    $expectedHash = (Get-FileHash -Algorithm SHA256 $Expected).Hash
    $actualHash = (Get-FileHash -Algorithm SHA256 $Actual).Hash
    if ($expectedHash -ne $actualHash) {
        throw "$Label hash mismatch: expected=$expectedHash actual=$actualHash"
    }
    Write-Host ("  {0}: {1}" -f $Label, $actualHash)
}

Assert-SameHash -Expected $fmpElf -Actual $arduinoElf -Label 'ELF SHA-256'
Assert-SameHash -Expected $fmpBin -Actual $arduinoBin -Label 'BIN SHA-256'

$applicationBytes = [System.IO.File]::ReadAllBytes($fmpBin)
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
    throw 'The FMP3 application image was not preserved at merged offset 0x10000.'
}

$defined = @(& $nm -C --defined-only $arduinoElf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while inspecting the published FMP3 ELF.'
}

foreach ($requiredSymbol in @('_start', '_kernel_start_dispatch', 'sta_ker')) {
    if (-not ($defined | Select-String "[ ]$requiredSymbol$")) {
        throw "Required FMP3 symbol was not found: $requiredSymbol"
    }
}
foreach ($forbiddenSymbol in @('app_main', 'vTaskStartScheduler', 'loopTask\(void\*\)')) {
    if ($defined | Select-String "[ ]$forbiddenSymbol$") {
        throw "Arduino/FreeRTOS symbol was unexpectedly linked: $forbiddenSymbol"
    }
}

Write-Host ''
Write-Host 'Arduino recipe override passed.'
Write-Host ('  FQBN: {0}' -f $Fqbn)
Write-Host ('  Merged image: {0} bytes' -f (Get-Item $mergedBin).Length)
Write-Host '  FMP3 application matches the merged image at offset 0x10000.'
Write-Host '  FMP3 symbols present; Arduino app_main and FreeRTOS scheduler absent.'
