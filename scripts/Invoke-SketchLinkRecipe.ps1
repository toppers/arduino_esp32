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

    #  Which vendored runtime profile the image is built on. Replaces
    #  -ProcessorCount: the kernel's processor count is a property of the
    #  profile now (m5-unified and all-in-one build with FMP3_PRC_NUM=2, the
    #  rest with 1), so asking for both would let them disagree.
    [ValidateSet('minimal', 'm5-unified', 'wifi-connect', 'all-in-one',
        'bt-classic')]
    [string]$Profile = 'minimal',

    #  Which chip the image is for. Forwarded to Build-SeamS3M5.ps1 and used
    #  for the toolchain's name, which carries the chip. Hardcoding esp32s3
    #  here is what kept the whole host-side path CoreS3-only even after
    #  Build-SeamS3M5.ps1 learned -Chip.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3'
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
        'ports\m5stack_xtensa\app\phase3'
}

$destinationElf = Join-Path $ArduinoBuildPath "$ProjectName.elf"
$destinationBin = Join-Path $ArduinoBuildPath "$ProjectName.bin"

if ($Mode -eq 'Link') {
    #  Split the same way scripts/fmp3_link.py and
    #  scripts/Invoke-PortableFmp3Recipe.ps1 do, for the same reasons.
    #
    #  Force-linked: every translation unit of the sketch, plus the bridge.
    #  On-demand (archived): everything else the builder compiled, so the
    #  linker takes a member only when the sketch refers to it. This used to
    #  pass just the one .ino object and the bridge, which left the library's
    #  own objects out of the link entirely - the same defect that stopped
    #  examples/LibraryInfo with an undefined libraryInfo(). Force-linking
    #  them all instead breaks the profiles they were not built for.
    $sketchDirectory = Join-Path $ArduinoBuildPath 'sketch'
    if (-not (Test-Path -LiteralPath $sketchDirectory)) {
        throw "Arduino sketch object directory was not found: $sketchDirectory"
    }
    #  Not just the .ino: a sketch folder may hold further .cpp/.c files.
    $sketchObjects = @(Get-ChildItem -LiteralPath $sketchDirectory `
        -Recurse -Filter '*.o' -File | Sort-Object FullName)
    $sketchObject = Join-Path $sketchDirectory "$ProjectName.cpp.o"
    if (-not (Test-Path -LiteralPath $sketchObject)) {
        throw "Arduino sketch object was not found: $sketchObject"
    }

    $librariesDirectory = Join-Path $ArduinoBuildPath 'libraries'
    $bridgeObjects = @(
        Get-ChildItem -LiteralPath $librariesDirectory `
            -Recurse -Filter 'ArduinoSketchBridge.cpp.o' -File
    )
    if ($bridgeObjects.Count -ne 1) {
        throw "Expected one ArduinoSketchBridge.cpp.o, found $($bridgeObjects.Count)."
    }

    $externalObjects = @($sketchObjects | ForEach-Object { $_.FullName }) +
        $bridgeObjects[0].FullName
    $archivedObjects = @(Get-ChildItem -LiteralPath $librariesDirectory `
        -Recurse -Filter '*.o' -File | Sort-Object FullName |
        Where-Object { $_.FullName -ne $bridgeObjects[0].FullName } |
        ForEach-Object { $_.FullName })

    #  'D' keeps the archive reproducible; duplicate member basenames are
    #  resolved through the symbol index, so two libraries may both contain a
    #  util.cpp.o. Derived from the compiler's name, which carries the chip.
    $externalArchive = ''
    if ($archivedObjects.Count -gt 0) {
        $toolchainCompiler = Get-ChildItem -LiteralPath (Join-Path `
            $env:LOCALAPPDATA 'Arduino15\packages\m5stack\tools\esp-x32') `
            -Recurse -Filter "xtensa-$Chip-elf-gcc.exe" -File |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($null -eq $toolchainCompiler) {
            throw 'The Xtensa toolchain was not found below tools\esp-x32.'
        }
        $ar = $toolchainCompiler.FullName -replace 'gcc\.exe$', 'ar.exe'
        if (-not (Test-Path -LiteralPath $ar)) {
            throw "The Xtensa archiver was not found: $ar"
        }
        [void](New-Item -ItemType Directory -Path $FmpBuildDirectory -Force)
        $externalArchive = Join-Path $FmpBuildDirectory 'libarduino_ondemand.a'
        if (Test-Path -LiteralPath $externalArchive) {
            Remove-Item -LiteralPath $externalArchive -Force
        }
        & $ar 'rcsD' $externalArchive @archivedObjects
        if ($LASTEXITCODE -ne 0) {
            throw "Archiving the Arduino objects failed (exit=$LASTEXITCODE)"
        }
    }

    $buildScript = Join-Path $M5ArduinoRoot 'scripts\Build-SeamS3M5.ps1'

    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $buildScript `
        -BuildDirectory $FmpBuildDirectory `
        -Profile $Profile `
        -Chip $Chip `
        -ApplicationDirectory $FmpApplicationDirectory `
        -ApplicationName $FmpApplicationName `
        -ExternalObjects ($externalObjects -join '|') `
        -ExternalArchive $externalArchive
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
