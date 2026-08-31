<#
.SYNOPSIS
    Builds and statically validates the credential-free Wi-Fi scan.
#>

[CmdletBinding()]
param(
    [string]$M5ArduinoRoot = '',
    #  Checkout of the reference port. The reference is the
    #  PUBLIC toppers/fmp3_esp_idf; there is nothing to derive this from, so
    #  it has to be given. The old default named a path on another machine and
    #  the repository it named is no longer the reference.
    [string]$Fmp3Repository = '',
    [string]$BuildDirectory = '',
    [string]$M5StackPackage =
        (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $M5ArduinoRoot 'build\phase7-wifi-scan-native'
}

$buildScript = Join-Path $M5ArduinoRoot 'scripts\Build-SeamS3M5.ps1'
$toolchainBin = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin'
$nm = Join-Path $toolchainBin 'xtensa-esp32s3-elf-nm.exe'

foreach ($required in @($buildScript, $Fmp3Repository, $nm)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

if (-not $SkipBuild) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $buildScript `
        -Fmp3Repository $Fmp3Repository `
        -BuildDirectory $BuildDirectory `
        -M5StackPackage $M5StackPackage `
        -SkipRomLinkSetup `
        -Variant wifi `
        -WifiApplication wifi_scan
    if ($LASTEXITCODE -ne 0) {
        throw "Wi-Fi scan build failed (exit=$LASTEXITCODE)"
    }
}

$cache = Join-Path $BuildDirectory 'CMakeCache.txt'
$ninja = Join-Path $BuildDirectory 'build.ninja'
$elf = Join-Path $BuildDirectory 'xip\fmp_xip.elf'
$bin = Join-Path $BuildDirectory 'xip\app_xip.bin'
foreach ($required in @($cache, $ninja, $elf, $bin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Expected artifact was not found: $required"
    }
}

if (-not (Select-String -LiteralPath $cache -SimpleMatch `
        'A1_VARIANT:STRING=wifi')) {
    throw 'The seam was not configured for the Wi-Fi variant.'
}
if (-not (Select-String -LiteralPath $cache -SimpleMatch `
        'A1_WIFI_APP:STRING=wifi_scan')) {
    throw 'The seam was not configured for the wifi_scan application.'
}
if (-not (Select-String -LiteralPath $ninja -SimpleMatch 'wifi_scan.c.obj')) {
    throw 'wifi_scan.c is not part of the generated build.'
}

$defined = @(& $nm -C --defined-only $elf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while inspecting the Wi-Fi scan ELF.'
}
foreach ($symbol in @(
        'main_task',
        'esp_wifi_init',
        'esp_wifi_start',
        'esp_wifi_scan_start',
        'esp_wifi_scan_get_ap_records')) {
    if (-not ($defined | Select-String -SimpleMatch " $symbol")) {
        throw "Required Wi-Fi scan symbol was not found: $symbol"
    }
}

$undefined = @(& $nm -u $elf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while checking undefined symbols.'
}
if ($undefined.Count -ne 0) {
    throw "The Wi-Fi scan ELF has $($undefined.Count) undefined symbol(s)."
}

Write-Host ''
Write-Host 'Credential-free Wi-Fi scan passed static validation.'
Get-FileHash -Algorithm SHA256 $elf, $bin |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  Variant: wifi / application: wifi_scan'
Write-Host '  Undefined symbols: 0'
Write-Host '  No SSID or password is required for this scan-only application.'
