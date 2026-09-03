<#
.SYNOPSIS
    Replacement for the Arduino link and application-image recipes.

.DESCRIPTION
    Link mode invokes the existing FMP3 CMake build and publishes its ELF and
    application image under the filenames expected by Arduino. Objcopy mode
    verifies that the FMP3 image is still present instead of running Arduino's
    default elf2image command, which writes an ESP-IDF ELF digest at offset
    0xb0 and would overwrite FMP3 data.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Link', 'Objcopy')]
    [string]$Mode,

    [Parameter(Mandatory)]
    [string]$ArduinoBuildPath,

    [Parameter(Mandatory)]
    [string]$ProjectName,

    [string]$M5ArduinoRoot = '',
    [string]$FmpBuildDirectory = '',

    #  Which vendored runtime profile to build. The default keeps what the
    #  retired -Variant m5 meant. Build-SeamS3M5.ps1 used to need an external
    #  toppers/fmp3_esp_idf checkout that this script had no parameter to pass,
    #  which is why every run of it died binding an empty -Path.
    [ValidateSet('minimal', 'm5-unified', 'wifi-connect', 'all-in-one',
        'bt-classic')]
    [string]$Profile = 'm5-unified',

    #  This mode publishes a STANDALONE FMP3 image - the sketch's own objects
    #  are deliberately not linked, which is the whole point of the override -
    #  so the application must not need the Arduino bridge. Most of them do:
    #  their cfg declares CRE_TSK(ARDUINO_TASK, ... toppers_arduino_task ...)
    #  and the link stops on an undefined toppers_arduino_task. The self-test
    #  applications (phase5_m5_selftest, phase6_smp_selftest) are the ones
    #  that stand alone. Empty means the profile default, which will only work
    #  for a profile whose default is standalone.
    [string]$ApplicationDirectory = '',
    [string]$ApplicationName = '',

    #  Forwarded to Build-SeamS3M5.ps1, which takes the same parameter.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot 'build\baseline-seam-s3-m5'
}

$destinationElf = Join-Path $ArduinoBuildPath "$ProjectName.elf"
$destinationBin = Join-Path $ArduinoBuildPath "$ProjectName.bin"
$sourceElf = Join-Path $FmpBuildDirectory 'xip\fmp_xip.elf'
$sourceBin = Join-Path $FmpBuildDirectory 'xip\app_xip.bin'

if ($Mode -eq 'Link') {
    $buildScript = Join-Path $M5ArduinoRoot 'scripts\Build-SeamS3M5.ps1'
    if (-not (Test-Path -LiteralPath $buildScript)) {
        throw "FMP3 build script was not found: $buildScript"
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $buildScript `
        -BuildDirectory $FmpBuildDirectory `
        -Profile $Profile `
        -ApplicationDirectory $ApplicationDirectory `
        -ApplicationName $ApplicationName `
        -Chip $Chip
    if ($LASTEXITCODE -ne 0) {
        throw "FMP3 build failed (exit=$LASTEXITCODE)"
    }

    foreach ($required in @($sourceElf, $sourceBin)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "FMP3 artifact was not generated: $required"
        }
    }

    Copy-Item -LiteralPath $sourceElf -Destination $destinationElf -Force
    Copy-Item -LiteralPath $sourceBin -Destination $destinationBin -Force
    Write-Host "Published FMP3 ELF: $destinationElf"
    Write-Host "Published FMP3 BIN: $destinationBin"
}
else {
    foreach ($required in @($destinationElf, $destinationBin)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Published FMP3 artifact was not found: $required"
        }
    }
    Write-Host 'Preserved FMP3 application image; Arduino elf2image was skipped.'
}
