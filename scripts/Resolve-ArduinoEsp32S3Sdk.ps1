<#
.SYNOPSIS
    Resolves and validates the per-chip SDK bundled with M5Stack Arduino core.
#>

[CmdletBinding()]
param(
    [string]$ArduinoData = '',
    [string]$CoreVersion = '3.3.8',

    #  The core ships one SDK tree per chip, laid out identically and
    #  named for the chip both in the tool directory and in the linker
    #  scripts inside it. Hardcoding esp32s3 here made -Chip esp32 resolve
    #  the S3 tree, so the LX6 stage build stopped on a missing
    #  esp32.rom.ld. scripts/arduino_sdk.py takes the same parameter.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    [switch]$AsJson
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ArduinoData)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        throw 'LOCALAPPDATA is unavailable; specify -ArduinoData.'
    }
    $ArduinoData = Join-Path $env:LOCALAPPDATA 'Arduino15'
}

$packageRoot = Join-Path $ArduinoData 'packages\m5stack'
$coreRoot = Join-Path $packageRoot "hardware\esp32\$CoreVersion"
$sdkRoot = Join-Path $packageRoot "tools\$Chip-libs\$CoreVersion"
$includeRoot = Join-Path $sdkRoot 'include'
$libraryRoot = Join-Path $sdkRoot 'lib'
$linkerScriptRoot = Join-Path $sdkRoot 'ld'
$versionsFile = Join-Path $sdkRoot 'versions.txt'

$required = [ordered]@{
    core = $coreRoot
    sdk = $sdkRoot
    versions = $versionsFile
    idfVersionHeader = Join-Path $includeRoot `
        'esp_common\include\esp_idf_version.h'
    xtensaCoreIsa = Join-Path $includeRoot `
        "xtensa\$Chip\include\xtensa\config\core-isa.h"
    peripheralLinkerScript = Join-Path $linkerScriptRoot `
        "$Chip.peripherals.ld"
    romLinkerScript = Join-Path $linkerScriptRoot "$Chip.rom.ld"
    socArchive = Join-Path $libraryRoot 'libsoc.a'
    wifiArchive = Join-Path $libraryRoot 'libesp_wifi.a'
    coexistArchive = Join-Path $libraryRoot 'libcoexist.a'
    phyArchive = Join-Path $linkerScriptRoot 'libphy.a'
    lwipArchive = Join-Path $libraryRoot 'liblwip.a'
    mbedtlsArchive = Join-Path $libraryRoot 'libmbedtls.a'
}

foreach ($item in $required.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $item.Value)) {
        throw "M5Stack Arduino SDK item is missing ($($item.Key)): $($item.Value)"
    }
}

$versions = Get-Content -LiteralPath $versionsFile -Encoding utf8
$idfLine = $versions | Where-Object { $_ -match '^esp-idf:\s+' } |
    Select-Object -First 1
if ($null -eq $idfLine) {
    throw "ESP-IDF version was not found in $versionsFile"
}
$idfVersion = ([regex]::Match($idfLine, 'v\d+\.\d+\.\d+')).Value
if ([string]::IsNullOrWhiteSpace($idfVersion)) {
    throw "Could not parse ESP-IDF version from: $idfLine"
}

$result = [ordered]@{
    arduinoData = [System.IO.Path]::GetFullPath($ArduinoData)
    package = 'm5stack:esp32'
    packageRoot = [System.IO.Path]::GetFullPath($packageRoot)
    coreVersion = $CoreVersion
    chip = $Chip
    coreRoot = [System.IO.Path]::GetFullPath($coreRoot)
    sdkRoot = [System.IO.Path]::GetFullPath($sdkRoot)
    espIdfVersion = $idfVersion
    includeRoot = [System.IO.Path]::GetFullPath($includeRoot)
    libraryRoot = [System.IO.Path]::GetFullPath($libraryRoot)
    linkerScriptRoot = [System.IO.Path]::GetFullPath($linkerScriptRoot)
}

if ($AsJson) {
    $result | ConvertTo-Json
}
else {
    [pscustomobject]$result
}
