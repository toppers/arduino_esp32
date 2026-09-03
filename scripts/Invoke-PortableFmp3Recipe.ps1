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
    [string]$Profile = 'minimal',

    #  Where M5GFX and M5Unified live, for the m5-unified runtime, which
    #  compiles them itself. Normally derived from the sketch build; see
    #  Resolve-M5LibrarySource. New-Fmp3PrebuiltStages.ps1 takes the same two.
    [string]$M5GfxSource = '',
    [string]$M5UnifiedSource = ''
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
    return ''
}

function Resolve-M5LibrarySource {
    param(
        [Parameter(Mandatory)]
        [string]$BuildPath,
        [Parameter(Mandatory)]
        [string]$LibraryName,
        [Parameter(Mandatory)]
        [string]$AnchorSource,
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$ExplicitSource,
        [Parameter(Mandatory)]
        [string]$LibraryRoot,
        #  Named in the failure message, so it has to match this script's own
        #  parameter spelling rather than be derived from $LibraryName.
        [Parameter(Mandatory)]
        [string]$ParameterName
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitSource)) {
        if (-not (Test-Path -LiteralPath $ExplicitSource)) {
            throw "$LibraryName source was not found: $ExplicitSource"
        }
        return [System.IO.Path]::GetFullPath($ExplicitSource)
    }

    #  What the sketch build itself selected, when it selected anything. This
    #  stays first so the runtime is compiled against the very same source the
    #  sketch was.
    $selected = Find-ArduinoLibrarySourceRoot -BuildPath $BuildPath `
        -LibraryName $LibraryName -AnchorSource $AnchorSource
    if (-not [string]::IsNullOrWhiteSpace($selected)) {
        return $selected
    }

    #  Nothing selected it, which is normal: arduino-cli compiles a library
    #  only when the sketch includes it, and not every sketch on this runtime
    #  does. examples/DualCore includes ToppersFMP3_ArduinoBridge.h and nothing
    #  else, so the m5-unified runtime - which needs M5GFX/M5Unified for its
    #  own application - had no source to build against and this stopped with
    #  "Could not resolve the M5GFX source selected by Arduino."
    #
    #  -LibraryRoot is <sketchbook>/libraries/<this library>, so its parent is
    #  the libraries directory arduino-cli is actually using, and a sibling
    #  there is the same copy arduino-cli would have picked. No guessing.
    #  The development tree, where -LibraryRoot is the repository itself, falls
    #  through to the sketchbook below.
    $candidates = @((Join-Path (Split-Path -Parent $LibraryRoot) "$LibraryName\src"))
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if (-not [string]::IsNullOrWhiteSpace($documents)) {
        $candidates += (Join-Path $documents "Arduino\libraries\$LibraryName\src")
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate $AnchorSource)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw ("The $Profile runtime compiles $LibraryName itself, and its " +
        "source was not found. The sketch build did not select it, and none " +
        "of these holds ${AnchorSource}: " + ($candidates -join '; ') +
        ". Pass -${ParameterName}.")
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

#  What the Arduino builder produced, split by how it reaches the linker.
#  The same split, for the same reasons, as collect_arduino_objects in
#  scripts/fmp3_link.py.
#
#  Force-linked: every translation unit of the sketch, plus the library
#  objects the runtime cannot work without - the bridge, and whatever the
#  profile itself requires.
#
#  On-demand (archived): everything else the builder compiled - the bundled
#  library's other sources, and any library the sketch pulls in. This used to
#  be hardcoded as "the one .ino object, the bridge, and for wifi-connect one
#  more", so no other object reached the linker at all:
#  examples/LibraryInfo calls toppers::fmp3::m5cores3::libraryInfo(), which
#  lives in src/ToppersFMP3_M5CoreS3.cpp, and the link stopped on an undefined
#  reference to it. Force-linking everything instead is not an option either:
#  ToppersFMP3_WiFi.cpp.o calls Wi-Fi symbols that exist only in the
#  wifi-connect runtime, so a minimal build would stop linking.
$sketchDirectory = Join-Path $ArduinoBuildPath 'sketch'
if (-not (Test-Path -LiteralPath $sketchDirectory)) {
    throw "Arduino sketch object directory was not found: $sketchDirectory"
}
#  Not just the .ino: a sketch folder may hold further .cpp/.c files, and the
#  builder compiles each of them into build/sketch. verify_package.py has a
#  multi-file sketch in its matrix for exactly this.
$sketchObjects = @(Get-ChildItem -LiteralPath $sketchDirectory `
    -Recurse -Filter '*.o' -File | Sort-Object FullName)
if ($sketchObjects.Count -eq 0) {
    throw "No Arduino sketch object was found below $sketchDirectory"
}
$sketchObject = Join-Path $sketchDirectory "$ProjectName.cpp.o"
if (-not (Test-Path -LiteralPath $sketchObject)) {
    throw "Arduino sketch object was not found: $sketchObject"
}

$librariesDirectory = Join-Path $ArduinoBuildPath 'libraries'
$requiredNames = @('ArduinoSketchBridge.cpp.o')
if ($Profile -eq 'wifi-connect') {
    $requiredNames += 'ToppersFMP3_WiFi.cpp.o'
}
$requiredObjects = @()
foreach ($requiredName in $requiredNames) {
    $hits = @(Get-ChildItem -LiteralPath $librariesDirectory `
        -Recurse -Filter $requiredName -File)
    if ($hits.Count -ne 1) {
        throw "Expected one $requiredName, found $($hits.Count)."
    }
    $requiredObjects += $hits[0].FullName
}

$requiredSet = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]$requiredObjects,
    [System.StringComparer]::OrdinalIgnoreCase)
$archivedObjects = @(Get-ChildItem -LiteralPath $librariesDirectory `
    -Recurse -Filter '*.o' -File | Sort-Object FullName |
    Where-Object { -not $requiredSet.Contains($_.FullName) } |
    ForEach-Object { $_.FullName })

$linkedObjects = @($sketchObjects | ForEach-Object { $_.FullName }) +
    $requiredObjects

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
    $m5gfxSource = Resolve-M5LibrarySource `
        -BuildPath $ArduinoBuildPath -LibraryName 'M5GFX' `
        -AnchorSource 'M5GFX.cpp' -ExplicitSource $M5GfxSource `
        -LibraryRoot $LibraryRoot -ParameterName 'M5GfxSource'
    $m5UnifiedSource = Resolve-M5LibrarySource `
        -BuildPath $ArduinoBuildPath -LibraryName 'M5Unified' `
        -AnchorSource 'M5Unified.cpp' -ExplicitSource $M5UnifiedSource `
        -LibraryRoot $LibraryRoot -ParameterName 'M5UnifiedSource'
    $sdkArguments += @(
        "-DM5GFX_SOURCE_ROOT=$m5gfxSource"
        "-DM5UNIFIED_SOURCE_ROOT=$m5UnifiedSource"
        "-DTOPPERS_LIBRARY_SOURCE_ROOT=$(Join-Path $LibraryRoot 'src')"
    )
}
#  Put the on-demand objects into an archive so the linker takes a member only
#  when the sketch actually refers to it. 'D' keeps the archive reproducible
#  (zeroed timestamps and uid/gid). Duplicate member basenames are allowed by
#  ar and resolved through the symbol index, so two libraries may both contain
#  a util.cpp.o. Mirrors stage_archive in fmp3_link.py.
$arduinoArchive = ''
if ($archivedObjects.Count -gt 0) {
    #  Derived from the compiler's name. ar carries the chip name too, so
    #  hardcoding one would leave the same thing behind that
    #  Resolve-ArduinoEsp32S3Sdk.ps1 did.
    $ar = $toolchainCompiler -replace 'gcc\.exe$', 'ar.exe'
    if (-not (Test-Path -LiteralPath $ar)) {
        throw "The Xtensa archiver was not found: $ar"
    }
    $arduinoArchive = Join-Path $fmpBuild 'libarduino_ondemand.a'
    [void](New-Item -ItemType Directory -Path $fmpBuild -Force)
    if (Test-Path -LiteralPath $arduinoArchive) {
        Remove-Item -LiteralPath $arduinoArchive -Force
    }
    & $ar 'rcsD' $arduinoArchive @archivedObjects
    if ($LASTEXITCODE -ne 0) {
        throw "Archiving the Arduino objects failed (exit=$LASTEXITCODE)."
    }
}

$originalPath = $env:PATH
try {
    $env:PATH = "$toolchainBin;$originalPath"
    $arduinoObjects = $linkedObjects -join ';'
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
        "-DARDUINO_OBJECTS:STRING=$arduinoObjects" `
        "-DARDUINO_ARCHIVE:STRING=$arduinoArchive"
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
