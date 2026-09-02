<#
.SYNOPSIS
    Replaces Arduino's final link with the packaged TOPPERS/FMP3 runtime.
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

    [string]$LibraryRoot = '',
    [string]$CMake = '',
    [string]$Ninja = '',
    [string]$ArduinoData = '',
    [string]$CoreVersion = '3.3.8',

    [ValidateSet('minimal', 'm5-unified', 'wifi-connect')]
    [string]$Profile = 'minimal'
)

$ErrorActionPreference = 'Stop'

function Find-LibraryRoot {
    foreach ($candidate in @(
            (Join-Path $PSScriptRoot '..'),
            (Join-Path $PSScriptRoot '..\..'))) {
        $resolved = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path -LiteralPath (Join-Path $resolved 'library.properties')) {
            return $resolved
        }
    }
    throw 'Could not locate library.properties relative to the recipe script.'
}

function Resolve-Program {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [string]$ExplicitPath,
        [string[]]$Candidates = @()
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            throw "$Name was not found: $ExplicitPath"
        }
        return [System.IO.Path]::GetFullPath($ExplicitPath)
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw "$Name was not found. Install it or specify its path."
}

function Find-ToolFile {
    param(
        [Parameter(Mandatory)]
        [string]$SearchRoot,
        [Parameter(Mandatory)]
        [string]$FileName
    )

    $matches = @(Get-ChildItem -LiteralPath $SearchRoot -Recurse `
        -Filter $FileName -File | Sort-Object FullName -Descending)
    if ($matches.Count -eq 0) {
        throw "$FileName was not found below $SearchRoot"
    }
    return $matches[0].FullName
}

function Find-ArduinoLibrarySourceRoot {
    param(
        [Parameter(Mandatory)]
        [string]$BuildPath,
        [Parameter(Mandatory)]
        [string]$LibraryName,
        [Parameter(Mandatory)]
        [string]$AnchorSource
    )

    $dependencyFiles = @(Get-ChildItem -LiteralPath (
            Join-Path $BuildPath "libraries\$LibraryName") -Recurse `
        -Filter "$AnchorSource.d" -File -ErrorAction SilentlyContinue)
    foreach ($dependencyFile in $dependencyFiles) {
        $dependencyText = Get-Content -LiteralPath $dependencyFile.FullName -Raw
        $escapedAnchor = [regex]::Escape($AnchorSource)
        $match = [regex]::Match(
            $dependencyText,
            "(?m)^\s*(.+?[\\/]src[\\/]$escapedAnchor)\s*\\?\r?$")
        if ($match.Success) {
            $sourceFile = $match.Groups[1].Value.Trim()
            if (Test-Path -LiteralPath $sourceFile) {
                return Split-Path -Parent $sourceFile
            }
        }
    }
    throw "Could not resolve the $LibraryName source selected by Arduino."
}

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = Find-LibraryRoot
}
$LibraryRoot = [System.IO.Path]::GetFullPath($LibraryRoot)

$destinationElf = Join-Path $ArduinoBuildPath "$ProjectName.elf"
$destinationBin = Join-Path $ArduinoBuildPath "$ProjectName.bin"
if ($Mode -eq 'Objcopy') {
    foreach ($required in @($destinationElf, $destinationBin)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "FMP3 artifact was not found: $required"
        }
    }
    Write-Host 'Preserved the TOPPERS/FMP3 application image.'
    exit 0
}

$developmentRuntime = Join-Path $LibraryRoot `
    'ports\m5stack_xtensa\runtime'
$packagedRuntime = Join-Path $LibraryRoot 'extras\runtime\port'
if (Test-Path -LiteralPath $developmentRuntime) {
    $runtime = $developmentRuntime
    $fmp3Core = Join-Path $LibraryRoot 'third_party\fmp3_core'
    $applicationRoot = Join-Path $LibraryRoot `
        'ports\m5stack_xtensa\app'
    $resolver = Join-Path $LibraryRoot `
        'scripts\Resolve-ArduinoEsp32S3Sdk.ps1'
}
elseif (Test-Path -LiteralPath $packagedRuntime) {
    $runtime = $packagedRuntime
    $fmp3Core = Join-Path $LibraryRoot 'extras\runtime\fmp3_core'
    $applicationRoot = Join-Path $LibraryRoot 'extras\runtime\app'
    $resolver = Join-Path $LibraryRoot `
        'extras\tools\Resolve-ArduinoEsp32S3Sdk.ps1'
}
else {
    throw "Packaged FMP3 runtime was not found below $LibraryRoot"
}

$applicationName = switch ($Profile) {
    'm5-unified' { 'phase5_m5_app' }
    'wifi-connect' { 'phase9_wifi_connect_app' }
    default { 'phase3_arduino_app' }
}
$applicationDirectoryName = switch ($Profile) {
    'm5-unified' { 'phase5' }
    'wifi-connect' { 'wifi_connect' }
    default { 'phase3' }
}
$application = Join-Path $applicationRoot $applicationDirectoryName
if ((Test-Path -LiteralPath $developmentRuntime) -and
    ($Profile -eq 'm5-unified')) {
    $application = Join-Path $LibraryRoot "fmp_app\$applicationDirectoryName"
}

foreach ($required in @(
        (Join-Path $runtime 'CMakeLists.txt'),
        (Join-Path $fmp3Core 'CMakeLists.txt'),
        (Join-Path $application "$applicationName.cfg"),
        $resolver)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "FMP3 runtime input was not found: $required"
    }
}

$resolverArguments = @{
    CoreVersion = $CoreVersion
}
if (-not [string]::IsNullOrWhiteSpace($ArduinoData)) {
    $resolverArguments.ArduinoData = $ArduinoData
}
$sdk = & $resolver @resolverArguments
if ($null -eq $sdk) {
    throw 'Resolving the M5Stack Arduino SDK failed.'
}

#  Candidates beyond PATH. Visual Studio ships both, which is where they are on
#  the current machine; the previous list named another developer's pinned
#  .pico-sdk versions and matched nothing here.
$visualStudioCMake = @(Get-ChildItem -Path (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio') `
    -Recurse -Filter 'cmake.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName)
$visualStudioNinja = @(Get-ChildItem -Path (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio') `
    -Recurse -Filter 'ninja.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName)
$cmakeProgram = Resolve-Program -Name 'cmake.exe' `
    -ExplicitPath $CMake -Candidates $visualStudioCMake
$ninjaProgram = Resolve-Program -Name 'ninja.exe' `
    -ExplicitPath $Ninja -Candidates $visualStudioNinja

$toolchainCompiler = Find-ToolFile `
    -SearchRoot (Join-Path $sdk.packageRoot 'tools\esp-x32') `
    -FileName 'xtensa-esp32s3-elf-gcc.exe'
$esptool = Find-ToolFile `
    -SearchRoot (Join-Path $sdk.packageRoot 'tools\esptool_py') `
    -FileName 'esptool.exe'
$toolchainBin = Split-Path -Parent $toolchainCompiler

$sketchObject = Join-Path $ArduinoBuildPath "sketch\$ProjectName.cpp.o"
$bridgeObjects = @(Get-ChildItem `
    -LiteralPath (Join-Path $ArduinoBuildPath 'libraries') `
    -Recurse -Filter 'ArduinoSketchBridge.cpp.o' -File)
if (-not (Test-Path -LiteralPath $sketchObject)) {
    throw "Arduino sketch object was not found: $sketchObject"
}
if ($bridgeObjects.Count -ne 1) {
    throw "Expected one ArduinoSketchBridge.cpp.o, found $($bridgeObjects.Count)."
}

$profileObjects = @()
if ($Profile -eq 'wifi-connect') {
    $wifiObjects = @(Get-ChildItem `
        -LiteralPath (Join-Path $ArduinoBuildPath 'libraries') `
        -Recurse -Filter 'ToppersFMP3_WiFi.cpp.o' -File)
    if ($wifiObjects.Count -ne 1) {
        throw "Expected one ToppersFMP3_WiFi.cpp.o, found $($wifiObjects.Count)."
    }
    $profileObjects += $wifiObjects[0].FullName
}

$fmpBuild = Join-Path $ArduinoBuildPath 'fmp3-runtime-build'
$sdkArguments = @(
    "-DARDUINO_SDK_LD_ROOT=$($sdk.linkerScriptRoot)"
)
if ($Profile -in @('m5-unified', 'wifi-connect')) {
    $sdkArguments += @(
        "-DARDUINO_SDK_INCLUDE_ROOT=$($sdk.includeRoot)"
        "-DARDUINO_SDK_LIBRARY_ROOT=$($sdk.libraryRoot)"
    )
}
if ($Profile -eq 'm5-unified') {
    $m5gfxSource = Find-ArduinoLibrarySourceRoot `
        -BuildPath $ArduinoBuildPath -LibraryName 'M5GFX' `
        -AnchorSource 'M5GFX.cpp'
    $m5UnifiedSource = Find-ArduinoLibrarySourceRoot `
        -BuildPath $ArduinoBuildPath -LibraryName 'M5Unified' `
        -AnchorSource 'M5Unified.cpp'
    $sdkArguments += @(
        "-DM5GFX_SOURCE_ROOT=$m5gfxSource"
        "-DM5UNIFIED_SOURCE_ROOT=$m5UnifiedSource"
        "-DTOPPERS_LIBRARY_SOURCE_ROOT=$(Join-Path $LibraryRoot 'src')"
    )
}
$originalPath = $env:PATH
try {
    $env:PATH = "$toolchainBin;$originalPath"
    $arduinoObjects = @(
        $sketchObject
        $bridgeObjects[0].FullName
        $profileObjects
    ) -join ';'
    & $cmakeProgram `
        -S $runtime `
        -B $fmpBuild `
        -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninjaProgram" `
        "-DCMAKE_TOOLCHAIN_FILE=$runtime\cmake\toolchain-xtensa-esp32s3.cmake" `
        "-DFMP3_CORE_ROOT=$fmp3Core" `
        "-DFMP3_APPLICATION_DIR=$application" `
        "-DFMP3_APPLICATION_NAME=$applicationName" `
        "-DFMP3_RUNTIME_PROFILE=$Profile" `
        @sdkArguments `
        "-DA1_ESPTOOL_EXECUTABLE=$esptool" `
        "-DARDUINO_OBJECTS:STRING=$arduinoObjects"
    if ($LASTEXITCODE -ne 0) {
        throw "Configuring the portable FMP3 runtime failed (exit=$LASTEXITCODE)."
    }

    & $cmakeProgram --build $fmpBuild --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Building the portable FMP3 runtime failed (exit=$LASTEXITCODE)."
    }
}
finally {
    $env:PATH = $originalPath
}

$sourceElf = Join-Path $fmpBuild 'xip\fmp_xip.elf'
$sourceBin = Join-Path $fmpBuild 'xip\app_xip.bin'
foreach ($required in @($sourceElf, $sourceBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Expected FMP3 output was not generated: $required"
    }
}

Copy-Item -LiteralPath $sourceElf -Destination $destinationElf -Force
Copy-Item -LiteralPath $sourceBin -Destination $destinationBin -Force
Write-Host "Published FMP3 ELF: $destinationElf"
Write-Host "Published FMP3 BIN: $destinationBin"
