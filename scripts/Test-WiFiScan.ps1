<#
.SYNOPSIS
    Builds and statically validates the credential-free Wi-Fi scan.

.DESCRIPTION
    Builds examples/WiFiScan through the sketch-link recipe, on the
    wifi-connect runtime, and checks the resulting image statically: the Wi-Fi
    scan entry points are present and no symbol is left undefined.

    It used to configure an EXTERNAL toppers/fmp3_esp_idf checkout through
    -Fmp3Repository, with -Variant wifi -WifiApplication wifi_scan, and assert
    on A1_VARIANT / A1_WIFI_APP in the resulting CMakeCache. None of that
    exists here: the runtime is vendored (ports/m5stack_xtensa/runtime), both
    Wi-Fi adapters live in the one wifi-connect profile, and those two cache
    variables are the external tree's. On top of that, Build-SeamS3M5.ps1
    demanded a patches\esp32_s3-windows-host-tools.patch that this repository
    does not contain, so the test could not pass even given the external tree.

    A standalone image is not an option for this profile: the wifi-connect
    application declares CRE_TSK(ARDUINO_TASK, ... toppers_arduino_task ...),
    so it only links with a sketch. That is why this now goes through the same
    recipe as Test-SketchBridge.ps1 and the other three rather than invoking
    the builder directly.
#>

[CmdletBinding()]
param(
    [string]$ArduinoCli = (@(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Get-Command 'arduino-cli' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1),
    [string]$M5StackPackage =
        (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$M5ArduinoRoot = '',
    [string]$ArduinoBuildPath = '',
    [string]$BuildDirectory = '',
    [string]$Fqbn = 'm5stack:esp32:m5stack_cores3',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($ArduinoBuildPath)) {
    $ArduinoBuildPath = Join-Path $M5ArduinoRoot 'build\arduino-phase7-wifi-scan'
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $M5ArduinoRoot 'build\phase7-wifi-scan-native'
}

$recipeScript = Join-Path $M5ArduinoRoot 'scripts\Invoke-SketchLinkRecipe.ps1'
$sketch = Join-Path $M5ArduinoRoot 'examples\WiFiScan'
$toolchainBin = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin'
$nm = Join-Path $toolchainBin 'xtensa-esp32s3-elf-nm.exe'

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
    "-FmpBuildDirectory `"$BuildDirectory`""
    "-FmpApplicationDirectory `"$M5ArduinoRoot\ports\m5stack_xtensa\app\wifi_connect`""
    '-FmpApplicationName phase9_wifi_connect_app'
    #  Both Wi-Fi adapters - connect and scan - are compiled into this one
    #  profile, so it is what carries wifi/adapter/toppers_wifi_scan.c.
    '-Profile wifi-connect'
) -join ' '

if (-not $SkipBuild) {
    & $ArduinoCli compile `
        --fqbn $Fqbn `
        --library $M5ArduinoRoot `
        --build-path $ArduinoBuildPath `
        --build-property "recipe.c.combine.pattern=$commonRecipeArguments -Mode Link" `
        --build-property "recipe.objcopy.bin.pattern=$commonRecipeArguments -Mode Objcopy" `
        $sketch
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

#  Asserted in the vendored runtime's own vocabulary. A1_VARIANT and
#  A1_WIFI_APP were the external tree's variables and never appear here.
if (-not (Select-String -LiteralPath $cache -SimpleMatch `
        'FMP3_RUNTIME_PROFILE:STRING=wifi-connect')) {
    throw 'The runtime was not configured for the wifi-connect profile.'
}
if (-not (Select-String -LiteralPath $ninja -SimpleMatch 'toppers_wifi_scan.c.obj')) {
    throw 'toppers_wifi_scan.c is not part of the generated build.'
}

$defined = @(& $nm -C --defined-only $elf)
if ($LASTEXITCODE -ne 0) {
    throw 'nm failed while inspecting the Wi-Fi scan ELF.'
}
#  main_task is gone from this list: it was the external tree's entry point.
#  Here the kernel starts sta_ker and the application runs the Arduino bridge
#  task, so those two are what "the image has an entry" means.
#
#  The scan adapter's own functions are asserted as well. The four esp_wifi_*
#  ones only say the IDF Wi-Fi API is linked, which the connect path would
#  satisfy too; toppers_fmp3_wifi_scan_networks is what makes this the SCAN
#  test.
foreach ($symbol in @(
        'sta_ker',
        'toppers_arduino_task',
        'toppers_fmp3_wifi_scan_networks',
        'toppers_fmp3_wifi_ssid',
        'toppers_fmp3_wifi_rssi',
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
Write-Host '  Profile: wifi-connect / sketch: examples\WiFiScan'
Write-Host '  Undefined symbols: 0'
#  examples/WiFiScan takes no credentials: it calls the scan and reports the
#  results. That is the sketch's property, not something this ELF can show.
Write-Host '  Scan-only sketch: no SSID or password is used.'
