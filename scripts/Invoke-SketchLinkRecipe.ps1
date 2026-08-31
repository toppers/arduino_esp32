<#
.SYNOPSIS
    Links Arduino-generated sketch and bridge objects into the FMP3 image.
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
    [string]$FmpApplicationDirectory = '',
    [string]$FmpApplicationName = 'phase3_arduino_app',
    [ValidateRange(1, 2)]
    [int]$ProcessorCount = 1
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($FmpBuildDirectory)) {
    $FmpBuildDirectory = Join-Path $M5ArduinoRoot 'build\phase3-seam-s3-m5'
}
if ([string]::IsNullOrWhiteSpace($FmpApplicationDirectory)) {
    $FmpApplicationDirectory = Join-Path $M5ArduinoRoot `
        'ports\esp32s3_m5cores3\app\phase3'
}

$destinationElf = Join-Path $ArduinoBuildPath "$ProjectName.elf"
$destinationBin = Join-Path $ArduinoBuildPath "$ProjectName.bin"

if ($Mode -eq 'Link') {
    $sketchObject = Join-Path $ArduinoBuildPath "sketch\$ProjectName.cpp.o"
    $bridgeObjects = @(
        Get-ChildItem -Path (Join-Path $ArduinoBuildPath 'libraries') `
            -Recurse -Filter 'ArduinoSketchBridge.cpp.o' -File
    )

    if (-not (Test-Path -LiteralPath $sketchObject)) {
        throw "Arduino sketch object was not found: $sketchObject"
    }
    if ($bridgeObjects.Count -ne 1) {
        throw "Expected one ArduinoSketchBridge.cpp.o, found $($bridgeObjects.Count)."
    }

    $buildScript = Join-Path $M5ArduinoRoot 'scripts\Build-SeamS3M5.ps1'
    $externalObjects = @($sketchObject, $bridgeObjects[0].FullName)

    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $buildScript `
        -BuildDirectory $FmpBuildDirectory `
        -SkipRomLinkSetup `
        -ExternalApplicationDirectory $FmpApplicationDirectory `
        -ExternalApplicationName $FmpApplicationName `
        -ProcessorCount $ProcessorCount `
        -ExternalObjects ($externalObjects -join '|')
    if ($LASTEXITCODE -ne 0) {
        throw "FMP3 Arduino bridge build failed (exit=$LASTEXITCODE)"
    }

    $sourceElf = Join-Path $FmpBuildDirectory 'xip\fmp_xip.elf'
    $sourceBin = Join-Path $FmpBuildDirectory 'xip\app_xip.bin'
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
    Write-Host 'Preserved the FMP3 application image.'
}
