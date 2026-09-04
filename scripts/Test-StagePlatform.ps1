<#
.SYNOPSIS
    Builds sketches from the platform install_platform.py assembles.

.DESCRIPTION
    The artifact that carries every board is the platform built from prebuilt
    stages - what install_platform.py assembles and what Boards Manager
    packages. Nothing in this suite built a sketch from it:
    The legacy library ZIP had its own test, which is gone with the ZIP; the
    rest of the host-side tests drive the seam path directly.

    So this is the shipping path, on Windows, for all three boards. What is
    Windows-specific about it, and therefore only checked here, is the chain:
    stages built by build_prebuilt_stages.py, the platform assembled on this
    host, Windows arduino-cli, and gen_esp32part through the link driver.
    scripts/verify_package.py does the same job on Linux from an installed
    Boards Manager package.

    Everything happens in -Sketchbook, which defaults to a temporary
    directory. The user's own sketchbook is never touched.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
      -File .\scripts\Test-StagePlatform.ps1
#>

[CmdletBinding()]
param(
    [string]$M5ArduinoRoot = '',

    [string]$ArduinoCli = (@(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Get-Command 'arduino-cli' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1),

    [string]$M5StackPackage = (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$ArduinoData = (Join-Path $env:LOCALAPPDATA 'Arduino15'),

    [string]$Python = ((Get-Command 'python' -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty Source)),

    #  Stages from build_prebuilt_stages.py. The parent of the per-chip
    #  directories, so one install covers every board.
    [string]$PrebuiltStageRoot = '',

    #  Isolated by default. Anything under here is created and removed by this
    #  test, so it must not be the user's own sketchbook.
    [string]$Sketchbook = '',

    #  Which boards to build. Default is all three.
    [ValidateSet('m5cores3_fmp3', 'm5sticks3_fmp3', 'm5core_fmp3')]
    [string[]]$Boards = @('m5cores3_fmp3', 'm5sticks3_fmp3', 'm5core_fmp3'),

    #  M5Unified is compiled by the Arduino builder for the m5 profile, which
    #  is most of this test's runtime. Skipping it still covers every board.
    [switch]$SkipM5Unified
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($PrebuiltStageRoot)) {
    $PrebuiltStageRoot = Join-Path $M5ArduinoRoot 'build\prebuilt'
}
if ([string]::IsNullOrWhiteSpace($Sketchbook)) {
    $Sketchbook = Join-Path $M5ArduinoRoot 'build\stage-platform\sketchbook'
}

#  What each board is and what it can build.
#
#  m5-unified is absent for the M5StickS3 on purpose: it does not work there,
#  and docs/m5sticks3-m5unified.md records the investigation - M5GFX's
#  autodetect finds no display, M5Unified falls back to board_M5AtomS3Lite,
#  and M5.begin fails for want of an SPI bus. Listing it here would assert
#  something known to be false.
#
#  bt-classic is the M5Core's alone: the ESP32-S3 has no BR/EDR radio.
#
#  LibraryInfo is on every board because it is the only example that calls
#  into the library itself (libraryInfo()), which is why
#  scripts/verify_package.py puts it on every profile too.
$boardMatrix = [ordered]@{
    'm5cores3_fmp3' = @{
        Chip = 'esp32s3'
        Builds = @(
            @{ Menu = 'minimal'; Sketch = 'LibraryInfo' }
            @{ Menu = 'm5'; Sketch = 'M5Unified'; NeedsM5Libraries = $true }
            @{ Menu = 'wificonnect'; Sketch = 'WiFiConnect' }
        )
    }
    'm5sticks3_fmp3' = @{
        Chip = 'esp32s3'
        Builds = @(
            @{ Menu = 'minimal'; Sketch = 'LibraryInfo' }
            @{ Menu = 'wificonnect'; Sketch = 'WiFiConnect' }
        )
    }
    'm5core_fmp3' = @{
        Chip = 'esp32'
        Builds = @(
            @{ Menu = 'minimal'; Sketch = 'LibraryInfo' }
            @{ Menu = 'm5'; Sketch = 'M5Unified'; NeedsM5Libraries = $true }
            @{ Menu = 'wificonnect'; Sketch = 'WiFiConnect' }
            @{ Menu = 'btclassic'; Sketch = 'BluetoothSPP' }
        )
    }
}

$installer = Join-Path $M5ArduinoRoot 'scripts\install_platform.py'
foreach ($required in @($ArduinoCli, $Python, $installer)) {
    if ([string]::IsNullOrWhiteSpace($required) -or
            -not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

#  Name the stages that are missing rather than letting arduino-cli fail on an
#  FQBN it cannot resolve, which says nothing about why.
$neededChips = @($Boards | ForEach-Object { $boardMatrix[$_].Chip } |
    Sort-Object -Unique)
foreach ($chip in $neededChips) {
    $chipRoot = Join-Path $PrebuiltStageRoot $chip
    if (-not (Test-Path -LiteralPath $chipRoot)) {
        throw ("No prebuilt stages for $chip below $PrebuiltStageRoot. Run " +
            "python scripts/build_prebuilt_stages.py --chip $chip first.")
    }
}
#  bt-classic is not in the shipped set, so it is built separately and its
#  absence should say so here rather than at the link.
if (('m5core_fmp3' -in $Boards) -and
        -not (Test-Path -LiteralPath (
            Join-Path $PrebuiltStageRoot 'esp32\bt-classic'))) {
    throw ("The bt-classic stage is missing. Run " +
        "python scripts/build_prebuilt_stages.py --chip esp32 --profiles bt-classic, " +
        "or pass -Boards without m5core_fmp3.")
}

$documents = [Environment]::GetFolderPath('MyDocuments')
$m5GfxLibrary = Join-Path $documents 'Arduino\libraries\M5GFX'
$m5UnifiedLibrary = Join-Path $documents 'Arduino\libraries\M5Unified'

$stageRoot = Split-Path -Parent $Sketchbook
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
[void](New-Item -ItemType Directory -Path $Sketchbook -Force)

& $Python $installer --sketchbook $Sketchbook `
    --prebuilt-stage-root $PrebuiltStageRoot
if ($LASTEXITCODE -ne 0) {
    throw "Assembling the platform failed (exit=$LASTEXITCODE)."
}

$installedBoards = Join-Path $Sketchbook 'hardware\toppers\esp32\boards.txt'
if (-not (Test-Path -LiteralPath $installedBoards)) {
    throw "The platform was not assembled: $installedBoards"
}
$installedBoardLines = @(Get-Content -LiteralPath $installedBoards -Encoding utf8)
foreach ($board in $Boards) {
    if (-not ($installedBoardLines | Select-String -SimpleMatch "$board.name=")) {
        throw "The assembled platform has no $board board."
    }
}

#  arduino-cli has to look here and nowhere else, or a platform in the user's
#  own sketchbook could answer instead.
$config = Join-Path $stageRoot 'arduino-cli.yaml'
@(
    'directories:'
    "  user: $Sketchbook"
    "  data: $ArduinoData"
    "  downloads: $(Join-Path $ArduinoData 'staging')"
) | Set-Content -LiteralPath $config -Encoding utf8

$results = [System.Collections.Generic.List[object]]::new()

foreach ($board in $Boards) {
    $chip = $boardMatrix[$board].Chip
    $nm = Join-Path $M5StackPackage `
        "tools\esp-x32\2601\bin\xtensa-$chip-elf-nm.exe"
    if (-not (Test-Path -LiteralPath $nm)) {
        throw "Required path was not found: $nm"
    }

    foreach ($build in $boardMatrix[$board].Builds) {
        if ($SkipM5Unified -and $build.NeedsM5Libraries) {
            Write-Host ('--- skipping {0} {1} ({2}) per -SkipM5Unified' -f
                $board, $build.Menu, $build.Sketch)
            continue
        }

        $fqbn = 'toppers:esp32:{0}:FMP3Runtime={1}' -f $board, $build.Menu
        $sketch = Join-Path $M5ArduinoRoot "examples\$($build.Sketch)"
        $buildPath = Join-Path $stageRoot ('{0}-{1}' -f $board, $build.Menu)

        $libraryArguments = @('--library', $M5ArduinoRoot)
        if ($build.NeedsM5Libraries) {
            foreach ($library in @($m5GfxLibrary, $m5UnifiedLibrary)) {
                if (-not (Test-Path -LiteralPath $library)) {
                    throw "The m5 profile needs the library sources: $library"
                }
                $libraryArguments += @('--library', $library)
            }
        }

        Write-Host ''
        Write-Host ('=== {0} {1} / {2} ===' -f $board, $build.Menu, $build.Sketch)
        & $ArduinoCli compile --config-file $config `
            --fqbn $fqbn `
            @libraryArguments `
            --build-path $buildPath `
            $sketch
        if ($LASTEXITCODE -ne 0) {
            throw ("Compiling {0} for {1} {2} failed (exit={3})." -f
                $build.Sketch, $board, $build.Menu, $LASTEXITCODE)
        }

        $projectName = '{0}.ino' -f $build.Sketch
        $elf = Join-Path $buildPath "$projectName.elf"
        $bin = Join-Path $buildPath "$projectName.bin"
        $mergedBin = Join-Path $buildPath "$projectName.merged.bin"
        foreach ($artifact in @($elf, $bin, $mergedBin)) {
            if (-not (Test-Path -LiteralPath $artifact)) {
                throw "Expected artifact was not found: $artifact"
            }
        }

        #  The same properties the seam-path tests assert, so this says the
        #  stage path produces the same kind of image: FMP3 started it, and
        #  no part of Arduino's own runtime came along.
        $defined = @(& $nm -C --defined-only $elf)
        if ($LASTEXITCODE -ne 0) {
            throw "nm failed while inspecting $elf"
        }
        foreach ($symbol in @('_start', '_kernel_start_dispatch', 'sta_ker')) {
            if (-not ($defined | Select-String "[ ]$symbol$")) {
                throw ("Required FMP3 symbol {0} is missing from {1} {2}." -f
                    $symbol, $board, $build.Menu)
            }
        }
        foreach ($symbol in @('app_main', 'vTaskStartScheduler',
                'loopTask\(void\*\)')) {
            if ($defined | Select-String "[ ]$symbol$") {
                throw ("Arduino/FreeRTOS symbol {0} was linked into {1} {2}." -f
                    $symbol, $board, $build.Menu)
            }
        }

        #  arduino-cli's merge step must leave the FMP3 application alone.
        #  It writes an ESP-IDF ELF digest at offset 0xb0 of what it thinks is
        #  its own image, which is why the recipe overrides objcopy at all.
        $applicationBytes = [System.IO.File]::ReadAllBytes($bin)
        $mergedStream = [System.IO.File]::OpenRead($mergedBin)
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
            throw ("The FMP3 application is not intact at merged offset " +
                "0x10000 for {0} {1}." -f $board, $build.Menu)
        }

        $results.Add([pscustomobject]@{
            Board = $board
            Runtime = $build.Menu
            Sketch = $build.Sketch
            Bytes = (Get-Item $bin).Length
            Sha256 = (Get-FileHash -Algorithm SHA256 $bin).Hash
        })
    }
}

if ($results.Count -eq 0) {
    throw 'No sketch was built; nothing was verified.'
}

Write-Host ''
Write-Host 'Stage platform passed static validation.'
$results | Format-Table -AutoSize Board, Runtime, Sketch, Bytes
Write-Host ('  Platform: {0}' -f (Join-Path $Sketchbook 'hardware\toppers\esp32'))
Write-Host ('  Stages:   {0}' -f $PrebuiltStageRoot)
Write-Host ('  Builds:   {0}' -f $results.Count)
Write-Host '  FMP3 symbols present; Arduino app_main and FreeRTOS scheduler absent.'
Write-Host '  FMP3 application intact at merged offset 0x10000 in every build.'
foreach ($result in $results) {
    Write-Host ('  {0}  {1} {2} {3}' -f
        $result.Sha256, $result.Board, $result.Runtime, $result.Sketch)
}
