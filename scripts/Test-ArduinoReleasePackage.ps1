<#
.SYNOPSIS
    Generates, installs, and compiles the Arduino Release asset ZIP in isolation.
#>

[CmdletBinding()]
param(
    [string]$M5ArduinoRoot = '',
    [string]$ArduinoCli =
        (@(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Get-Command 'arduino-cli' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1),
    [string]$ArduinoData =
        (Join-Path $env:LOCALAPPDATA 'Arduino15'),
    [string]$M5GfxLibrary =
        (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino\libraries\M5GFX'),
    [string]$M5UnifiedLibrary =
        (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino\libraries\M5Unified'),
    [string]$Fqbn = 'm5stack:esp32:m5stack_cores3'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
$releaseDirectory = Join-Path $M5ArduinoRoot 'build\release'
$generator = Join-Path $M5ArduinoRoot 'scripts\New-ArduinoReleasePackage.ps1'
foreach ($required in @(
        $ArduinoCli, $ArduinoData, $M5GfxLibrary, $M5UnifiedLibrary, $generator)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $generator -AllowDirty
if ($LASTEXITCODE -ne 0) {
    throw "Release package generation failed (exit=$LASTEXITCODE)."
}

$properties = Get-Content -LiteralPath (
    Join-Path $M5ArduinoRoot 'library.properties') -Encoding utf8
$version = ($properties | Where-Object { $_ -match '^version=' } |
    Select-Object -First 1).Substring('version='.Length).Trim()
$zip = Join-Path $releaseDirectory "ToppersFMP3-M5CoreS3-$version.zip"

$testRoot = Join-Path $releaseDirectory 'install-test'
$sketchbook = Join-Path $testRoot 'sketchbook'
$downloads = Join-Path $testRoot 'downloads'
$buildPath = Join-Path $testRoot 'build'
$fmp3BuildPath = Join-Path $testRoot 'fmp3-build'
$blinkBuildPath = Join-Path $testRoot 'blink-build'
$dualCoreBuildPath = Join-Path $testRoot 'dual-core-build'
$m5UnifiedBuildPath = Join-Path $testRoot 'm5-unified-build'
$wifiBuildPath = Join-Path $testRoot 'wifi-build'
$wifiConnectBuildPath = Join-Path $testRoot 'wifi-connect-build'
$config = Join-Path $testRoot 'arduino-cli.yaml'
if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
foreach ($directory in @(
        $sketchbook, $downloads, $buildPath, $fmp3BuildPath,
        $blinkBuildPath, $dualCoreBuildPath, $m5UnifiedBuildPath,
        $wifiBuildPath, $wifiConnectBuildPath)) {
    [void](New-Item -ItemType Directory -Path $directory -Force)
}

@"
directories:
  data: $($ArduinoData.Replace('\', '/'))
  downloads: $($downloads.Replace('\', '/'))
  user: $($sketchbook.Replace('\', '/'))
library:
  enable_unsafe_install: true
"@ | Set-Content -LiteralPath $config -Encoding utf8

& $ArduinoCli lib install --config-file $config --zip-path $zip
if ($LASTEXITCODE -ne 0) {
    throw "Installing the generated ZIP failed (exit=$LASTEXITCODE)."
}

$installedRoot = Join-Path $sketchbook 'libraries\ToppersFMP3-M5CoreS3'
$example = Join-Path $installedRoot 'examples\LibraryInfo'
$fmp3Example = Join-Path $installedRoot 'examples\Fmp3Minimal'
$blinkExample = Join-Path $installedRoot 'examples\Blink'
$dualCoreExample = Join-Path $installedRoot 'examples\DualCore'
$m5UnifiedExample = Join-Path $installedRoot 'examples\M5Unified'
$wifiExample = Join-Path $installedRoot 'examples\WiFiScan'
$wifiConnectExample = Join-Path $installedRoot 'examples\WiFiConnect'
foreach ($required in @(
        (Join-Path $installedRoot 'library.properties'),
        (Join-Path $installedRoot 'extras\manifest.json'),
        $example,
        $fmp3Example,
        $blinkExample,
        $dualCoreExample,
        $m5UnifiedExample,
        $wifiExample,
        $wifiConnectExample,
        (Join-Path $installedRoot `
            'extras\runtime\port\wifi\prebuilt\wpa2\libsupplicant.a'),
        (Join-Path $installedRoot `
            'extras\runtime\port\wifi\prebuilt\wpa2\libmbedcrypto.a'),
        (Join-Path $installedRoot `
            'extras\runtime\port\wifi\prebuilt\wpa2\WPA_SUPPLICANT_COPYING.txt'),
        (Join-Path $installedRoot `
            'extras\runtime\port\wifi\prebuilt\wpa2\MBEDTLS_LICENSE.txt'),
        (Join-Path $installedRoot `
            'extras\runtime\port\wifi\prebuilt\wpa2\ESP_IDF_LICENSE.txt'),
        (Join-Path $installedRoot `
            'extras\tools\Install-ArduinoIdeIntegration.ps1'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Installed package content was not found: $required"
    }
}

$wpa2Archives = [ordered]@{
    'extras\runtime\port\wifi\prebuilt\wpa2\libsupplicant.a' =
        '212FAAFE03512E07DE7ED67EFC49E65AE6F25370361CD4D3B02B52AFB1C4F173'
    'extras\runtime\port\wifi\prebuilt\wpa2\libmbedcrypto.a' =
        '3242C8FA215A4F9E38EE0B3EEADA88D26012B1DB4DA08B4F0ED9E443CCA760F7'
}
foreach ($archive in $wpa2Archives.GetEnumerator()) {
    $actual = (Get-FileHash -LiteralPath (
        Join-Path $installedRoot $archive.Key) -Algorithm SHA256).Hash
    if ($actual -ne $archive.Value) {
        throw "Installed WPA2 archive checksum mismatch: $($archive.Key)"
    }
}

& $ArduinoCli compile --config-file $config `
    --fqbn $Fqbn `
    --build-path $buildPath `
    $example
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed LibraryInfo example failed (exit=$LASTEXITCODE)."
}

$installer = Join-Path $installedRoot `
    'extras\tools\Install-ArduinoIdeIntegration.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File $installer `
    -LibraryRoot $installedRoot `
    -Sketchbook $sketchbook
if ($LASTEXITCODE -ne 0) {
    throw "Installing the TOPPERS/FMP3 board failed (exit=$LASTEXITCODE)."
}

$installedBoards = Join-Path $sketchbook 'hardware\toppers\esp32\boards.txt'
$installedBoardLines = @(
    Get-Content -LiteralPath $installedBoards -Encoding utf8)
$runtimeMenuIndex = [Array]::IndexOf(
    $installedBoardLines, 'menu.FMP3Runtime=FMP3 Runtime')
$firstBoardIndex = [Array]::FindIndex(
    $installedBoardLines,
    [Predicate[string]] { param($line) $line -match '^m5cores3_fmp3\.' })
if (($runtimeMenuIndex -lt 0) -or ($runtimeMenuIndex -ge $firstBoardIndex)) {
    throw 'FMP3 Runtime must be declared with the global menus before board definitions.'
}
#  An earlier change retired 'dual' and 'wifi'; the surviving keys are
#  unchanged so a saved board selection keeps working.
foreach ($requiredMenuLine in @(
        'm5cores3_fmp3.menu.FMP3Runtime.minimal.build.toppers_profile=minimal',
        'm5cores3_fmp3.menu.FMP3Runtime.m5.build.toppers_profile=m5-unified',
        'm5cores3_fmp3.menu.FMP3Runtime.wificonnect.build.toppers_profile=wifi-connect')) {
    if ($requiredMenuLine -notin $installedBoardLines) {
        throw "Installed board menu entry was not found: $requiredMenuLine"
    }
}


& $ArduinoCli compile --config-file $config `
    --fqbn 'toppers:esp32:m5cores3_fmp3' `
    --build-path $fmp3BuildPath `
    $fmp3Example
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed Fmp3Minimal example failed (exit=$LASTEXITCODE)."
}


& $ArduinoCli compile --config-file $config `
    --fqbn 'toppers:esp32:m5cores3_fmp3' `
    --build-path $blinkBuildPath `
    $blinkExample
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed Blink example failed (exit=$LASTEXITCODE)."
}


& $ArduinoCli compile --config-file $config `
    --fqbn 'toppers:esp32:m5cores3_fmp3:FMP3Runtime=dual' `
    --build-path $dualCoreBuildPath `
    $dualCoreExample
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed DualCore example failed (exit=$LASTEXITCODE)."
}


& $ArduinoCli compile --config-file $config `
    --fqbn 'toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5' `
    --library $M5GfxLibrary `
    --library $M5UnifiedLibrary `
    --build-path $m5UnifiedBuildPath `
    $m5UnifiedExample
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed M5Unified example failed (exit=$LASTEXITCODE)."
}


& $ArduinoCli compile --config-file $config `
    --fqbn 'toppers:esp32:m5cores3_fmp3:FMP3Runtime=wifi' `
    --build-path $wifiBuildPath `
    $wifiExample
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed WiFiScan example failed (exit=$LASTEXITCODE)."
}


& $ArduinoCli compile --config-file $config `
    --fqbn 'toppers:esp32:m5cores3_fmp3:FMP3Runtime=wificonnect' `
    --build-path $wifiConnectBuildPath `
    $wifiConnectExample
if ($LASTEXITCODE -ne 0) {
    throw "Compiling the installed WiFiConnect example failed (exit=$LASTEXITCODE)."
}

$elf = Join-Path $buildPath 'LibraryInfo.ino.elf'
$bin = Join-Path $buildPath 'LibraryInfo.ino.bin'
foreach ($artifact in @($elf, $bin)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected installed-package artifact was not found: $artifact"
    }
}

$fmp3Elf = Join-Path $fmp3BuildPath 'Fmp3Minimal.ino.elf'
$fmp3Bin = Join-Path $fmp3BuildPath 'Fmp3Minimal.ino.bin'
$fmp3Merged = Join-Path $fmp3BuildPath 'Fmp3Minimal.ino.merged.bin'
foreach ($artifact in @($fmp3Elf, $fmp3Bin, $fmp3Merged)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected FMP3 artifact was not found: $artifact"
    }
}

$blinkElf = Join-Path $blinkBuildPath 'Blink.ino.elf'
$blinkBin = Join-Path $blinkBuildPath 'Blink.ino.bin'
$blinkMerged = Join-Path $blinkBuildPath 'Blink.ino.merged.bin'
foreach ($artifact in @($blinkElf, $blinkBin, $blinkMerged)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected Blink artifact was not found: $artifact"
    }
}

$dualCoreElf = Join-Path $dualCoreBuildPath 'DualCore.ino.elf'
$dualCoreBin = Join-Path $dualCoreBuildPath 'DualCore.ino.bin'
$dualCoreMerged = Join-Path $dualCoreBuildPath 'DualCore.ino.merged.bin'
foreach ($artifact in @($dualCoreElf, $dualCoreBin, $dualCoreMerged)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected DualCore artifact was not found: $artifact"
    }
}

$m5UnifiedElf = Join-Path $m5UnifiedBuildPath 'M5Unified.ino.elf'
$m5UnifiedBin = Join-Path $m5UnifiedBuildPath 'M5Unified.ino.bin'
$m5UnifiedMerged = Join-Path $m5UnifiedBuildPath 'M5Unified.ino.merged.bin'
foreach ($artifact in @($m5UnifiedElf, $m5UnifiedBin, $m5UnifiedMerged)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected M5Unified artifact was not found: $artifact"
    }
}

$wifiElf = Join-Path $wifiBuildPath 'WiFiScan.ino.elf'
$wifiBin = Join-Path $wifiBuildPath 'WiFiScan.ino.bin'
$wifiMerged = Join-Path $wifiBuildPath 'WiFiScan.ino.merged.bin'
foreach ($artifact in @($wifiElf, $wifiBin, $wifiMerged)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected WiFiScan artifact was not found: $artifact"
    }
}

$nm = Get-ChildItem -LiteralPath (
    Join-Path $ArduinoData 'packages\m5stack\tools\esp-x32') `
    -Recurse -Filter 'xtensa-esp32s3-elf-nm.exe' -File |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if ($null -eq $nm) {
    throw 'xtensa-esp32s3-elf-nm.exe was not found.'
}
$symbols = @(& $nm.FullName -C --defined-only $fmp3Elf)
foreach ($requiredSymbol in @(
        '_kernel_start_dispatch',
        'setup()',
        'loop()',
        'toppers_arduino_task')) {
    if (-not ($symbols | Select-String -SimpleMatch " $requiredSymbol")) {
        throw "Required FMP3 symbol was not found: $requiredSymbol"
    }
}
foreach ($forbiddenSymbol in @(
        ' app_main',
        ' vTaskStartScheduler',
        ' loopTask(void*)')) {
    if ($symbols | Select-String -SimpleMatch $forbiddenSymbol) {
        throw "Arduino/FreeRTOS symbol was linked: $forbiddenSymbol"
    }
}

$blinkSymbols = @(& $nm.FullName -C --defined-only $blinkElf)
foreach ($requiredSymbol in @(
        '_kernel_start_dispatch',
        'blinkState',
        'blinkTransitions',
        'toppers_arduino_task')) {
    if (-not ($blinkSymbols | Select-String -SimpleMatch $requiredSymbol)) {
        throw "Required Blink symbol was not found: $requiredSymbol"
    }
}

#  The shipped application does not carry the self-test, so the
#  monitor task, the PRC2 demo worker and their counters are deliberately
#  absent here. Test-Smp.ps1 builds phase6_smp_selftest and asserts on
#  those; what a release build must show is the SMP kernel and the bridge.
$dualCoreSymbols = @(& $nm.FullName -C --defined-only $dualCoreElf)
foreach ($requiredSymbol in @(
        '_kernel_start_dispatch',
        'toppers_arduino_task',
        'chip_ipi_send')) {
    if (-not ($dualCoreSymbols | Select-String -SimpleMatch $requiredSymbol)) {
        throw "Required DualCore symbol was not found: $requiredSymbol"
    }
}

#  Likewise the M5 monitor task belongs to phase5_m5_selftest and
#  is checked by Test-M5Unified.ps1. The adapter API is what ships.
$m5UnifiedSymbols = @(& $nm.FullName -C --defined-only $m5UnifiedElf)
foreach ($requiredSymbol in @(
        '_kernel_start_dispatch',
        'toppers_m5_begin',
        'toppers_m5_update',
        'M5')) {
    if (-not ($m5UnifiedSymbols | Select-String -SimpleMatch $requiredSymbol)) {
        throw "Required M5Unified symbol was not found: $requiredSymbol"
    }
}
$m5WrapCount = @(
    $m5UnifiedSymbols | Select-String -SimpleMatch ' T __wrap__').Count
if ($m5WrapCount -ne 13) {
    throw "Expected 13 M5.begin/update wrappers, found $m5WrapCount."
}

$wifiSymbols = @(& $nm.FullName -C --defined-only $wifiElf)
foreach ($requiredSymbol in @(
        '_kernel_start_dispatch',
        'WiFi',
        'ToppersFMP3WiFiClass::scanNetworks()',
        'toppers_fmp3_wifi_scan_networks',
        'esp_wifi_init',
        'esp_wifi_scan_start',
        'esp_wifi_scan_get_ap_records')) {
    if (-not ($wifiSymbols | Select-String -SimpleMatch $requiredSymbol)) {
        throw "Required WiFiScan symbol was not found: $requiredSymbol"
    }
}
foreach ($forbiddenSymbol in @(
        ' app_main',
        ' vTaskStartScheduler',
        ' loopTask(void*)')) {
    if ($wifiSymbols | Select-String -SimpleMatch $forbiddenSymbol) {
        throw "Arduino/FreeRTOS symbol was linked into WiFiScan: $forbiddenSymbol"
    }
}

$applicationBytes = [System.IO.File]::ReadAllBytes($fmp3Bin)
$mergedStream = [System.IO.File]::OpenRead($fmp3Merged)
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
    throw 'FMP3 application does not match merged image offset 0x10000.'
}

#  This asks a different question from the leak check in
#  New-ArduinoReleasePackage.ps1: not "did a personal path get in" but "does a
#  build from the INSTALLED package still reach back into the development
#  tree". Both roots are derived from the tree being tested, so it works for
#  whoever runs it.
#
#  A third entry was removed: a hard-coded absolute path to where the
#  reference checkout used to sit on one machine. No build produced from
#  this tree can name it, and keeping it meant carrying one person's
#  directory layout in the source forever.
$forbiddenRoots = @(
    (Join-Path ([System.IO.Path]::GetFullPath($M5ArduinoRoot)) 'ports'),
    (Join-Path ([System.IO.Path]::GetFullPath($M5ArduinoRoot)) 'third_party')
)
$buildOptions = Join-Path $buildPath 'build.options.json'
if (Test-Path -LiteralPath $buildOptions) {
    $optionsText = Get-Content -LiteralPath $buildOptions -Raw
    foreach ($forbidden in $forbiddenRoots) {
        if ($optionsText.Contains($forbidden)) {
            throw "Installed-package build refers to the development tree: $forbidden"
        }
    }
}

$fmp3BuildText = @(
    Get-ChildItem -LiteralPath (
        Join-Path $fmp3BuildPath 'fmp3-runtime-build') `
        -Recurse -File |
        Where-Object {
            $_.Name -in @('CMakeCache.txt', 'build.ninja')
        } |
        ForEach-Object {
            Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        }
) -join "`n"
foreach ($forbidden in $forbiddenRoots) {
    if ($fmp3BuildText.Contains($forbidden)) {
        throw "Packaged FMP3 build refers to a development tree: $forbidden"
    }
}

$m5UnifiedBuildText = @(
    Get-ChildItem -LiteralPath (
        Join-Path $m5UnifiedBuildPath 'fmp3-runtime-build') `
        -Recurse -File |
        Where-Object {
            $_.Name -in @('CMakeCache.txt', 'build.ninja')
        } |
        ForEach-Object {
            Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        }
) -join "`n"
foreach ($forbidden in $forbiddenRoots) {
    if ($m5UnifiedBuildText.Contains($forbidden)) {
        throw "Packaged M5Unified build refers to a development tree: $forbidden"
    }
}

$wifiBuildText = @(
    Get-ChildItem -LiteralPath (
        Join-Path $wifiBuildPath 'fmp3-runtime-build') `
        -Recurse -File |
        Where-Object {
            $_.Name -in @('CMakeCache.txt', 'build.ninja')
        } |
        ForEach-Object {
            Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        }
) -join "`n"
foreach ($forbidden in $forbiddenRoots) {
    if ($wifiBuildText.Contains($forbidden)) {
        throw "Packaged WiFiScan build refers to a development tree: $forbidden"
    }
}

$dualCoreBuildText = @(
    Get-ChildItem -LiteralPath (
        Join-Path $dualCoreBuildPath 'fmp3-runtime-build') `
        -Recurse -File |
        Where-Object {
            $_.Name -in @('CMakeCache.txt', 'build.ninja')
        } |
        ForEach-Object {
            Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
        }
) -join "`n"
foreach ($forbidden in $forbiddenRoots) {
    if ($dualCoreBuildText.Contains($forbidden)) {
        throw "Packaged DualCore build refers to a development tree: $forbidden"
    }
}

Write-Host ''
Write-Host 'Arduino Release asset ZIP passed isolated installation validation.'
Write-Host "  Installed: $installedRoot"
Write-Host "  FQBN:      $Fqbn"
Write-Host '  FMP3 FQBN: toppers:esp32:m5cores3_fmp3'
Write-Host '  Blink FQBN: toppers:esp32:m5cores3_fmp3'
Write-Host '  DualCore FQBN: toppers:esp32:m5cores3_fmp3:FMP3Runtime=dual'
Write-Host '  M5Unified FQBN: toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5'
Write-Host '  WiFi FQBN: toppers:esp32:m5cores3_fmp3:FMP3Runtime=wifi'
Get-FileHash -Algorithm SHA256 `
    $zip, $elf, $bin, $fmp3Elf, $fmp3Bin, $blinkElf, $blinkBin,
    $dualCoreElf, $dualCoreBin, $m5UnifiedElf, $m5UnifiedBin,
    $wifiElf, $wifiBin |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
