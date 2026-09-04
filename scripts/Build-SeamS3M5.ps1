<#
.SYNOPSIS
    Builds the FMP3 XIP image from the runtime vendored in this repository.

.DESCRIPTION
    Produces <BuildDirectory>/xip/fmp_xip.elf and app_xip.bin from
    ports/m5stack_xtensa/runtime plus third_party/fmp3_core, and refuses the
    result if any symbol is still undefined.

    This used to configure an EXTERNAL checkout of toppers/fmp3_esp_idf, given
    by -Fmp3Repository. That path could not be taken by any of its callers:

      - -Fmp3Repository defaulted to '' and Assert-Path rejects an empty
        string, so it was mandatory in effect, yet Invoke-FmpImageRecipe.ps1
        and Invoke-SketchLinkRecipe.ps1 had no parameter to pass it. The five
        tests that go through them all died with
            Cannot bind argument to parameter 'Path' because it is an
            empty string.
      - Even given a checkout, this script required A1_ESPTOOL_EXECUTABLE to
        be present in that repository's CMakeLists.txt and otherwise said
            (in Japanese) apply patches\esp32_s3-windows-host-tools.patch
        The public snapshot does not have that variable, and no patches/
        directory exists in this repository. So the path was unreachable even
        with the external tree in hand.

    Everything those callers need is vendored here: the kernel
    (third_party/fmp3_core), the runtime (ports/m5stack_xtensa/runtime), and
    every application they name - phase3_arduino_app,
    phase4_freertos_app, phase5_m5_selftest, phase6_smp_selftest and
    phase9_wifi_connect_app. scripts/build_prebuilt_stages.py already drives
    that tree on Windows
    with nothing but CMake, Ninja and the M5Stack core's toolchain, so this
    follows them rather than keeping a dependency the repository has already
    vendored away. Git Bash is no longer needed either: the ROM linker-script
    setup it ran was for the external tree.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
      -File .\scripts\Build-SeamS3M5.ps1 `
      -Profile minimal -BuildDirectory build\phase3 `
      -ExternalObjects 'C:\a\sketch.cpp.o|C:\a\ArduinoSketchBridge.cpp.o'
#>

[CmdletBinding()]
param(
    [string]$BuildDirectory = '',

    #  The vendored runtime's own vocabulary. The retired -Variant/-Fmp3Repository
    #  pair mapped onto the external tree's A1_VARIANT/A1_WIFI_APP, which do not
    #  exist here: 'wifi' is the wifi-connect profile, and both Wi-Fi adapters
    #  (connect and scan) are compiled into it.
    [ValidateSet('minimal', 'm5-unified', 'wifi-connect', 'all-in-one',
        'bt-classic')]
    [string]$Profile = 'm5-unified',

    #  Which application the image runs. Empty means the profile's default,
    #  the same mapping build_prebuilt_stages.py uses. Callers that want a
    #  self-test application (phase5_m5_selftest, phase6_smp_selftest) or one
    #  that is not a profile default (phase4_freertos_app) name it here.
    [string]$ApplicationDirectory = '',
    [string]$ApplicationName = '',

    #  Arduino-generated objects to FORCE-link, '|'-separated. Kept as a
    #  single string because that is how Arduino recipe lines pass lists.
    [string]$ExternalObjects = '',

    #  An archive of Arduino objects to offer ON DEMAND, so the linker takes a
    #  member only for a symbol still undefined. Force-linking these would
    #  break the profiles they were not built for; see ARDUINO_ARCHIVE in
    #  ports/m5stack_xtensa/runtime/cmake/xip_build.cmake.
    [string]$ExternalArchive = '',

    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    [string]$LibraryRoot = '',
    [string]$CMake = '',
    [string]$Ninja = '',
    [string]$ArduinoData = '',
    [string]$CoreVersion = '3.3.8',

    #  m5-unified and all-in-one compile M5GFX and M5Unified themselves.
    [string]$M5GfxSource = '',
    [string]$M5UnifiedSource = '',

    [int]$Parallel = 8,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $LibraryRoot 'build\baseline-seam-s3-m5'
}

# Windows PowerShell turns a native command's stderr into ErrorRecords, and with
# $ErrorActionPreference = 'Stop' that aborts the script even when the command
# succeeded. CMake writes warnings to stderr, so run native commands with
# 'Continue' and judge them by their exit code.
function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$What
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) {
        throw "$What failed (exit=$code)."
    }
}

function Resolve-Program {
    param([string]$Name, [string]$ExplicitPath)
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            throw "$Name was not found: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }
    throw "$Name was not found. Install it or pass its path."
}

function Find-ToolFile {
    param([string]$SearchRoot, [string]$FileName)
    $found = @(Get-ChildItem -LiteralPath $SearchRoot -Recurse -Filter $FileName `
        -File | Sort-Object FullName -Descending)
    if ($found.Count -eq 0) {
        throw "$FileName was not found below $SearchRoot"
    }
    return $found[0].FullName
}

$cmakeProgram = Resolve-Program -Name 'cmake.exe' -ExplicitPath $CMake
$ninjaProgram = Resolve-Program -Name 'ninja.exe' -ExplicitPath $Ninja

$resolverArguments = @{ CoreVersion = $CoreVersion; Chip = $Chip }
if (-not [string]::IsNullOrWhiteSpace($ArduinoData)) {
    $resolverArguments.ArduinoData = $ArduinoData
}
$sdk = & (Join-Path $PSScriptRoot 'Resolve-ArduinoEsp32S3Sdk.ps1') `
    @resolverArguments

#  The toolchain and the archiver are named for the chip.
$toolchainCompiler = Find-ToolFile `
    -SearchRoot (Join-Path $sdk.packageRoot 'tools\esp-x32') `
    -FileName "xtensa-$Chip-elf-gcc.exe"
$esptool = Find-ToolFile `
    -SearchRoot (Join-Path $sdk.packageRoot 'tools\esptool_py') `
    -FileName 'esptool.exe'
$toolchainBin = Split-Path -Parent $toolchainCompiler
$nm = $toolchainCompiler -replace 'gcc\.exe$', 'nm.exe'
if (-not (Test-Path -LiteralPath $nm)) {
    throw "The Xtensa nm was not found: $nm"
}

#  Same mapping as build_prebuilt_stages.py, so a profile means the same
#  application in both places unless a caller says otherwise.
if ([string]::IsNullOrWhiteSpace($ApplicationName)) {
    $ApplicationName = switch ($Profile) {
        'm5-unified' { 'phase5_m5_app' }
        'wifi-connect' { 'phase9_wifi_connect_app' }
        'all-in-one' { 'allinone_app' }
        'bt-classic' { 'bt_classic_app' }
        default { 'phase3_arduino_app' }
    }
}
if ([string]::IsNullOrWhiteSpace($ApplicationDirectory)) {
    $directoryName = switch ($Profile) {
        'm5-unified' { 'phase5' }
        'wifi-connect' { 'wifi_connect' }
        'all-in-one' { 'allinone' }
        'bt-classic' { 'bt_classic' }
        default { 'phase3' }
    }
    #  The m5-unified and all-in-one applications live outside ports/ in the
    #  development tree, the same split build_prebuilt_stages.py uses.
    $ApplicationDirectory = if ($Profile -in @('m5-unified', 'all-in-one')) {
        Join-Path $LibraryRoot "fmp_app\$directoryName"
    }
    else {
        Join-Path $LibraryRoot "ports\m5stack_xtensa\app\$directoryName"
    }
}
$ApplicationDirectory = [System.IO.Path]::GetFullPath($ApplicationDirectory)

$runtime = Join-Path $LibraryRoot 'ports\m5stack_xtensa\runtime'
$fmp3Core = Join-Path $LibraryRoot 'third_party\fmp3_core'
foreach ($required in @(
        (Join-Path $runtime 'CMakeLists.txt'),
        (Join-Path $fmp3Core 'CMakeLists.txt'),
        (Join-Path $ApplicationDirectory "$ApplicationName.cfg"))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "FMP3 build input was not found: $required"
    }
}

if (('m5-unified' -eq $Profile) -or ('all-in-one' -eq $Profile)) {
    if ([string]::IsNullOrWhiteSpace($M5GfxSource) -or
            [string]::IsNullOrWhiteSpace($M5UnifiedSource)) {
        $documents = [Environment]::GetFolderPath('MyDocuments')
        $sketchbookLibraries = Join-Path $documents 'Arduino\libraries'
        if ([string]::IsNullOrWhiteSpace($M5GfxSource)) {
            $M5GfxSource = Join-Path $sketchbookLibraries 'M5GFX\src'
        }
        if ([string]::IsNullOrWhiteSpace($M5UnifiedSource)) {
            $M5UnifiedSource = Join-Path $sketchbookLibraries 'M5Unified\src'
        }
    }
    foreach ($required in @($M5GfxSource, $M5UnifiedSource)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "$Profile needs the library sources: $required"
        }
    }
}

$externalObjectPaths = @(
    $ExternalObjects -split '\|' |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
foreach ($externalObject in $externalObjectPaths) {
    if (-not (Test-Path -LiteralPath $externalObject)) {
        throw "Arduino external object was not found: $externalObject"
    }
}
if ((-not [string]::IsNullOrWhiteSpace($ExternalArchive)) -and
        -not (Test-Path -LiteralPath $ExternalArchive)) {
    throw "Arduino external archive was not found: $ExternalArchive"
}

if ($Clean -and (Test-Path -LiteralPath $BuildDirectory)) {
    Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
}

$configureArguments = @(
    '-S', $runtime
    '-B', $BuildDirectory
    '-G', 'Ninja'
    "-DCMAKE_MAKE_PROGRAM=$ninjaProgram"
    "-DCMAKE_TOOLCHAIN_FILE=$runtime\cmake\toolchain-xtensa-$Chip.cmake"
    "-DA1_CHIP=$Chip"
    "-DFMP3_CORE_ROOT=$fmp3Core"
    "-DFMP3_APPLICATION_DIR=$ApplicationDirectory"
    "-DFMP3_APPLICATION_NAME=$ApplicationName"
    #  Typed, so it lands in CMakeCache.txt as
    #  FMP3_RUNTIME_PROFILE:STRING rather than :UNINITIALIZED.
    #  Test-WiFiScan.ps1 and Test-Smp.ps1 assert on it.
    "-DFMP3_RUNTIME_PROFILE:STRING=$Profile"
    "-DARDUINO_SDK_LD_ROOT=$($sdk.linkerScriptRoot)"
    "-DA1_ESPTOOL_EXECUTABLE=$esptool"
    "-DARDUINO_OBJECTS:STRING=$($externalObjectPaths -join ';')"
    "-DARDUINO_ARCHIVE:STRING=$ExternalArchive"
)
if ($Profile -in @('m5-unified', 'wifi-connect', 'all-in-one', 'bt-classic')) {
    $configureArguments += @(
        "-DARDUINO_SDK_INCLUDE_ROOT=$($sdk.includeRoot)"
        "-DARDUINO_SDK_LIBRARY_ROOT=$($sdk.libraryRoot)"
    )
}
if ($Profile -in @('m5-unified', 'all-in-one')) {
    $configureArguments += @(
        "-DM5GFX_SOURCE_ROOT=$M5GfxSource"
        "-DM5UNIFIED_SOURCE_ROOT=$M5UnifiedSource"
        "-DTOPPERS_LIBRARY_SOURCE_ROOT=$(Join-Path $LibraryRoot 'src')"
    )
}

$originalPath = $env:PATH
$originalEpoch = $env:SOURCE_DATE_EPOCH
try {
    $env:PATH = "$toolchainBin;$originalPath"
    #  Kept from the external-tree version: the image embeds a build date, and
    #  pinning it is what makes two builds of the same input compare equal.
    $env:SOURCE_DATE_EPOCH = '1500000000'

    Invoke-Native -FilePath $cmakeProgram -Arguments $configureArguments `
        -What "Configuring the $Profile FMP3 runtime"
    Invoke-Native -FilePath $cmakeProgram `
        -Arguments @('--build', $BuildDirectory, '--parallel', "$Parallel") `
        -What "Building the $Profile FMP3 image"
}
finally {
    $env:PATH = $originalPath
    $env:SOURCE_DATE_EPOCH = $originalEpoch
}

$xipDirectory = Join-Path $BuildDirectory 'xip'
$application = Join-Path $xipDirectory 'app_xip.bin'
$elf = Join-Path $xipDirectory 'fmp_xip.elf'
foreach ($required in @($application, $elf)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "FMP3 artifact was not generated: $required"
    }
}

$undefined = @(& $nm -u $elf)
if ($LASTEXITCODE -ne 0) {
    throw ('Inspecting undefined symbols with nm failed (exit={0})' -f $LASTEXITCODE)
}
if ($undefined.Count -ne 0) {
    throw ('Undefined symbols remain:{0}{1}' -f
        [Environment]::NewLine, ($undefined -join [Environment]::NewLine))
}

Write-Host ''
Write-Host 'Build completed.'
Write-Host ("  Profile:     {0}" -f $Profile)
Write-Host ("  Application: {0} ({1})" -f $ApplicationName, $ApplicationDirectory)
Write-Host ("  Chip:        {0}" -f $Chip)
if ($externalObjectPaths.Count -gt 0) {
    Write-Host ("  Arduino objects: {0}" -f $externalObjectPaths.Count)
}
Get-FileHash -Algorithm SHA256 $application, $elf |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  Undefined symbols: 0'
