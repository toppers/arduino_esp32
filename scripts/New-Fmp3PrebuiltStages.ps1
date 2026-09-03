<#
.SYNOPSIS
    Builds the sketch-independent FMP3 stage for each runtime profile.

.DESCRIPTION
    This is the release-time half of the split: CMake, Ninja
    and Python run here, on a developer machine, and produce stages that a
    sketch build can link against with nothing but the toolchain and esptool
    that ship with the M5Stack Arduino core.

    One directory per profile is produced under -OutputDirectory, each holding
    objs/, ld/, optionally lib/, link-manifest.json and objects.rsp. See
    ports/m5stack_xtensa/runtime/cmake/prebuilt_stage.cmake.

    The stages are sketch-independent because cfg is fixed per profile; only the
    final link depends on the sketch.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
      -File .\scripts\New-Fmp3PrebuiltStages.ps1 `
      -CMake 'C:\path\to\cmake.exe' -Ninja 'C:\path\to\ninja.exe'
#>

[CmdletBinding()]
param(
    #  'all-in-one' is EXPERIMENTAL: M5Unified + SMP + Wi-Fi in one
    #  runtime. Not in the default set and not shipped.
    [ValidateSet('minimal', 'm5-unified', 'wifi-connect', 'all-in-one')]
    [string[]]$Profiles = @('minimal', 'm5-unified', 'wifi-connect'),

    [string]$LibraryRoot = '',
    [string]$OutputDirectory = '',
    [string]$WorkDirectory = '',
    [string]$CMake = '',
    [string]$Ninja = '',
    [string]$ArduinoData = '',
    [string]$CoreVersion = '3.3.8',
    [string]$M5GfxSource = '',
    [string]$M5UnifiedSource = '',

    #  Stages are laid out per chip: the M5Core (plain ESP32, LX6) and the
    #  CoreS3 (ESP32-S3) each get their own set, and one platform holds both.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    #  Build the self-test flavour of each application instead of the one that
    #  ships. The self-test adds a monitor task that checks the runtime and
    #  prints PASS or FAILED.
    #  Those belong to the test suite, not to the product, so the stages that
    #  go into the Boards Manager package are built WITHOUT this switch. Use a
    #  separate -OutputDirectory so the two sets do not overwrite each other.
    #  Only m5-unified has a self-test application; the other profiles build
    #  the same thing either way. (The dual-core self-test, phase6_smp_selftest,
    #  is still built by Test-Smp.ps1 on the legacy cmake path - the
    #  profile is gone, the SMP isolation test is not.)
    [switch]$SelfTest,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path (Join-Path $LibraryRoot 'build\prebuilt') $Chip
}
if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
    $WorkDirectory = Join-Path (Join-Path $LibraryRoot 'build\prebuilt-work') $Chip
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

$cmakeProgram = Resolve-Program -Name 'cmake.exe' -ExplicitPath $CMake
$ninjaProgram = Resolve-Program -Name 'ninja.exe' -ExplicitPath $Ninja

$resolverArguments = @{ CoreVersion = $CoreVersion; Chip = $Chip }
if (-not [string]::IsNullOrWhiteSpace($ArduinoData)) {
    $resolverArguments.ArduinoData = $ArduinoData
}
$sdk = & (Join-Path $PSScriptRoot 'Resolve-ArduinoEsp32S3Sdk.ps1') `
    @resolverArguments

function Find-ToolFile {
    param([string]$SearchRoot, [string]$FileName)
    $found = @(Get-ChildItem -LiteralPath $SearchRoot -Recurse -Filter $FileName `
        -File | Sort-Object FullName -Descending)
    if ($found.Count -eq 0) {
        throw "$FileName was not found below $SearchRoot"
    }
    return $found[0].FullName
}

#  The toolchain is named for the chip. Hardcoding the S3 one made
#  -Chip esp32 build with the wrong compiler while the parameter said
#  otherwise - it was accepted and silently wrong.
$toolchainCompiler = Find-ToolFile `
    -SearchRoot (Join-Path $sdk.packageRoot 'tools\esp-x32') `
    -FileName "xtensa-$Chip-elf-gcc.exe"
$esptool = Find-ToolFile `
    -SearchRoot (Join-Path $sdk.packageRoot 'tools\esptool_py') `
    -FileName 'esptool.exe'
$toolchainBin = Split-Path -Parent $toolchainCompiler

# m5-unified compiles M5GFX and M5Unified on the CMake side, so the stage has to
# be built against the same sources the sketch build would use.
if (('m5-unified' -in $Profiles) -or ('all-in-one' -in $Profiles)) {
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
            throw "m5-unified needs the library sources: $required"
        }
    }
}

$runtime = Join-Path $LibraryRoot 'ports\m5stack_xtensa\runtime'
$fmp3Core = Join-Path $LibraryRoot 'third_party\fmp3_core'
if (-not (Test-Path -LiteralPath (Join-Path $fmp3Core 'CMakeLists.txt'))) {
    throw "fmp3_core submodule is not checked out: $fmp3Core"
}

if ($Clean) {
    # Only the profiles being built. Cleaning the whole output directory would
    # delete the stages of profiles not named in -Profiles, which is how an
    # install ended up with one stage instead of the full set.
    foreach ($profileName in $Profiles) {
        foreach ($stale in @((Join-Path $OutputDirectory $profileName),
                (Join-Path $WorkDirectory $profileName))) {
            if (Test-Path -LiteralPath $stale) {
                Remove-Item -LiteralPath $stale -Recurse -Force
            }
        }
    }
}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)

$originalPath = $env:PATH
$results = [System.Collections.Generic.List[object]]::new()
try {
    $env:PATH = "$toolchainBin;$originalPath"
    foreach ($profileName in $Profiles) {
        $applicationName = switch ($profileName) {
            'm5-unified' {
                if ($SelfTest) { 'phase5_m5_selftest' } else { 'phase5_m5_app' }
            }
            'wifi-connect' { 'phase9_wifi_connect_app' }
            'all-in-one' { 'allinone_app' }
            default { 'phase3_arduino_app' }
        }
        $applicationDirectoryName = switch ($profileName) {
            'm5-unified' { 'phase5' }
            'wifi-connect' { 'wifi_connect' }
            'all-in-one' { 'allinone' }
            default { 'phase3' }
        }
        # The m5-unified application lives outside ports/ in the development
        # tree, the same split Invoke-PortableFmp3Recipe.ps1 uses.
        $application = if ($profileName -in @('m5-unified', 'all-in-one')) {
            Join-Path $LibraryRoot "fmp_app\$applicationDirectoryName"
        }
        else {
            Join-Path $LibraryRoot `
                "ports\m5stack_xtensa\app\$applicationDirectoryName"
        }

        $build = Join-Path $WorkDirectory $profileName
        $stage = Join-Path $OutputDirectory $profileName

        $configureArguments = @(
            '-S', $runtime
            '-B', $build
            '-G', 'Ninja'
            "-DCMAKE_MAKE_PROGRAM=$ninjaProgram"
            "-DCMAKE_TOOLCHAIN_FILE=$runtime\cmake\toolchain-xtensa-$Chip.cmake"
            #  build_prebuilt_stages.py passes this; without it the CMake side
            #  falls back to its default chip and the stage is built for the
            #  wrong one.
            "-DA1_CHIP=$Chip"
            "-DFMP3_CORE_ROOT=$fmp3Core"
            "-DFMP3_APPLICATION_DIR=$application"
            "-DFMP3_APPLICATION_NAME=$applicationName"
            "-DFMP3_RUNTIME_PROFILE=$profileName"
            "-DARDUINO_SDK_LD_ROOT=$($sdk.linkerScriptRoot)"
            "-DA1_ESPTOOL_EXECUTABLE=$esptool"
        )
        if ($profileName -in @('m5-unified', 'wifi-connect', 'all-in-one')) {
            $configureArguments += @(
                "-DARDUINO_SDK_INCLUDE_ROOT=$($sdk.includeRoot)"
                "-DARDUINO_SDK_LIBRARY_ROOT=$($sdk.libraryRoot)"
            )
        }
        if ($profileName -in @('m5-unified', 'all-in-one')) {
            $configureArguments += @(
                "-DM5GFX_SOURCE_ROOT=$M5GfxSource"
                "-DM5UNIFIED_SOURCE_ROOT=$M5UnifiedSource"
                "-DTOPPERS_LIBRARY_SOURCE_ROOT=$(Join-Path $LibraryRoot 'src')"
            )
        }

        Write-Host ''
        Write-Host "=== staging $profileName ==="
        Invoke-Native -FilePath $cmakeProgram -Arguments $configureArguments `
            -What "Configuring $profileName"
        Invoke-Native -FilePath $cmakeProgram `
            -Arguments @('--build', $build, '--target', 'fmp3_prebuilt', '--parallel') `
            -What "Staging $profileName"

        $produced = Join-Path $build 'prebuilt'
        if (-not (Test-Path -LiteralPath (Join-Path $produced 'link-manifest.json'))) {
            throw "Stage was not produced for $profileName."
        }
        if (Test-Path -LiteralPath $stage) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
        Copy-Item -LiteralPath $produced -Destination $stage -Recurse

        $manifest = Get-Content -LiteralPath (Join-Path $stage 'link-manifest.json') `
            -Raw | ConvertFrom-Json
        $size = [math]::Round(((Get-ChildItem -LiteralPath $stage -Recurse -File |
            Measure-Object -Property Length -Sum).Sum / 1MB), 1)
        $results.Add([pscustomobject]@{
            Profile = $profileName
            Objects = $manifest.objectCount
            SizeMB = $size
            Stage = $stage
        })
    }
}
finally {
    $env:PATH = $originalPath
}

Write-Host ''
Write-Host 'Prebuilt FMP3 stages'
$results | Format-Table -AutoSize
Write-Host "Output: $OutputDirectory"
