<#
.SYNOPSIS
    Installs or removes the M5CoreS3 (TOPPERS/FMP3) Arduino board platform.
#>

[CmdletBinding()]
param(
    [string]$LibraryRoot = '',
    [string]$Sketchbook = '',
    [string]$ArduinoData = '',
    [string]$CoreVersion = '3.3.8',

    #  When a stage root is given the board links against
    #  prebuilt FMP3 stages instead of running the whole FMP3 build, so a sketch
    #  build needs neither CMake, Ninja nor a cfg generator. The stages and the
    #  driver are copied into the platform, and the recipe is written with
    #  Arduino variables only, which is what makes it portable to macOS/Linux.
    #  Generate the stages with scripts/New-Fmp3PrebuiltStages.ps1.
    [string]$PrebuiltStageRoot = '',
    #  Interpreter for the driver. A frozen per-OS build replaces this in the
    #  released package.
    [string]$PythonExecutable = '',

    #  Which chip's prebuilt stages this board uses. esp32 is for the plain
    #  M5Core (LX6) once its port is vendored here.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    [switch]$Uninstall
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
    throw 'Could not locate library.properties relative to this script.'
}

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = Find-LibraryRoot
}
$LibraryRoot = [System.IO.Path]::GetFullPath($LibraryRoot)

if ([string]::IsNullOrWhiteSpace($Sketchbook)) {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if ([string]::IsNullOrWhiteSpace($documents)) {
        throw 'Documents folder is unavailable; specify -Sketchbook.'
    }
    $Sketchbook = Join-Path $documents 'Arduino'
}
$Sketchbook = [System.IO.Path]::GetFullPath($Sketchbook)

$platformRoot = Join-Path $Sketchbook 'hardware\toppers\esp32'
$allowedRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $Sketchbook 'hardware\toppers'))
$allowedPrefix = $allowedRoot.TrimEnd('\', '/') + `
    [System.IO.Path]::DirectorySeparatorChar
$resolvedPlatform = [System.IO.Path]::GetFullPath($platformRoot)
if (-not $resolvedPlatform.StartsWith(
        $allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Platform target escapes the expected sketchbook location: $platformRoot"
}

$marker = Join-Path $platformRoot '.toppers-fmp3-platform.json'

function Remove-InstalledPlatform {
    if (-not (Test-Path -LiteralPath $platformRoot)) {
        return
    }
    if (-not (Test-Path -LiteralPath $marker)) {
        throw "Refusing to remove an unrecognized platform directory: $platformRoot"
    }

    #  Junctions are no longer created, but an install from an earlier
    #  version may still have them. Detaching them first keeps the recursive
    #  delete from following the link into the M5Stack platform.
    foreach ($junctionName in @('cores', 'libraries', 'tools', 'variants')) {
        $legacyJunction = Join-Path $platformRoot $junctionName
        if (-not (Test-Path -LiteralPath $legacyJunction)) {
            continue
        }
        $item = Get-Item -LiteralPath $legacyJunction -Force
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            [System.IO.Directory]::Delete($legacyJunction)
        }
    }
    Remove-Item -LiteralPath $platformRoot -Recurse -Force
}

if ($Uninstall) {
    Remove-InstalledPlatform
    Write-Host "Removed TOPPERS/FMP3 Arduino board platform: $platformRoot"
    exit 0
}

$resolverCandidates = @(
    (Join-Path $LibraryRoot 'scripts\Resolve-ArduinoEsp32S3Sdk.ps1'),
    (Join-Path $LibraryRoot 'extras\tools\Resolve-ArduinoEsp32S3Sdk.ps1'))
$resolver = $resolverCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if ($null -eq $resolver) {
    throw 'Resolve-ArduinoEsp32S3Sdk.ps1 was not found in the library.'
}

$recipeCandidates = @(
    (Join-Path $LibraryRoot 'scripts\Invoke-PortableFmp3Recipe.ps1'),
    (Join-Path $LibraryRoot 'extras\tools\Invoke-PortableFmp3Recipe.ps1'))
$recipe = $recipeCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if ($null -eq $recipe) {
    throw 'Invoke-PortableFmp3Recipe.ps1 was not found in the library.'
}

$resolverArguments = @{
    CoreVersion = $CoreVersion
}
if (-not [string]::IsNullOrWhiteSpace($ArduinoData)) {
    $resolverArguments.ArduinoData = $ArduinoData
}
$sdk = & $resolver @resolverArguments
$sourcePlatform = $sdk.coreRoot
$sourceBoards = Join-Path $sourcePlatform 'boards.txt'
$sourcePlatformFile = Join-Path $sourcePlatform 'platform.txt'

foreach ($required in @(
        $sourceBoards,
        $sourcePlatformFile,
        (Join-Path $sourcePlatform 'programmers.txt'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "M5Stack platform input was not found: $required"
    }
}

#  Validate everything that can fail before the existing platform is removed.
#  Removing first meant a bad argument left a half-deleted platform behind, and
#  because the marker file went with it the next run refused to continue.
$driverSource = $null
if (-not [string]::IsNullOrWhiteSpace($PrebuiltStageRoot)) {
    if (-not (Test-Path -LiteralPath $PrebuiltStageRoot)) {
        throw "Prebuilt stage root was not found: $PrebuiltStageRoot"
    }
    if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
        $found = Get-Command 'python.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -eq $found) {
            $found = Get-Command 'python3' -ErrorAction SilentlyContinue |
                Select-Object -First 1
        }
        if ($null -eq $found) {
            throw 'Python was not found. Pass -PythonExecutable.'
        }
        $PythonExecutable = $found.Source
    }
    if (-not (Test-Path -LiteralPath $PythonExecutable)) {
        throw "Python was not found: $PythonExecutable"
    }

    $driverSource = @(
        (Join-Path $LibraryRoot 'scripts\fmp3_link.py'),
        (Join-Path $LibraryRoot 'extras\tools\fmp3_link.py')) |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if ($null -eq $driverSource) {
        throw 'fmp3_link.py was not found in the library.'
    }
}

Remove-InstalledPlatform
[void](New-Item -ItemType Directory -Path $platformRoot -Force)

#
#  Reference the core instead of linking to it.
#
#  This used to create NTFS junctions for cores, libraries, tools and variants.
#  Junctions exist only on Windows, and a Boards Manager package cannot create
#  them at all, so they blocked both macOS/Linux and packaging.
#
#  boards.txt now says build.core=m5stack:esp32 and
#  build.variant=m5stack:m5stack_cores3 (Arduino's core reference), so
#  build.core.path and build.variant.path resolve into the M5Stack platform
#  while runtime.platform.path stays ours, which is what the fmp3-tools and
#  fmp3-prebuilt references rely on.
#
#  Only tools/ still needs to exist here, because it is reached through
#  runtime.platform.path. The partition CSVs (86 KB) are what the build reads;
#  espota, gen_insights_package and ide-debug are not part of this platform.
#
#  gen_esp32part.exe (6.0 MB) is only copied for the legacy path. With
#  prebuilt stages the partition recipe goes through the link driver on every
#  host, so nothing references it, and it was a Windows-only binary in a
#  package whose whole point is running everywhere - dropping it takes the
#  platform archive from 7.8 MB to 2.3 MB. The legacy path keeps the inherited
#  recipe and therefore still needs it.
#
#  The 29 KB gen_esp32part.py is always kept, so the inherited non-Windows tool
#  definition still resolves if something outside platform.txt reaches for it.
#
$toolsDestination = Join-Path $platformRoot 'tools'
[void](New-Item -ItemType Directory -Path $toolsDestination -Force)
$sourceTools = Join-Path $sourcePlatform 'tools'
Copy-Item -LiteralPath (Join-Path $sourceTools 'partitions') `
    -Destination $toolsDestination -Recurse -Force
$toolFiles = @('gen_esp32part.py')
if ([string]::IsNullOrWhiteSpace($PrebuiltStageRoot)) {
    $toolFiles += 'gen_esp32part.exe'
}
foreach ($toolFile in $toolFiles) {
    $sourceFile = Join-Path $sourceTools $toolFile
    if (Test-Path -LiteralPath $sourceFile) {
        Copy-Item -LiteralPath $sourceFile -Destination $toolsDestination -Force
    }
}

$sourceBoardLines = Get-Content -LiteralPath $sourceBoards -Encoding utf8 |
    Where-Object {
        $_ -match '^menu\.' -or $_ -match '^m5stack_cores3\.'
    } |
    ForEach-Object {
        $_ -replace '^m5stack_cores3\.', 'm5cores3_fmp3.'
}
$sourceBoardLines = $sourceBoardLines | ForEach-Object {
    if ($_ -eq 'm5cores3_fmp3.name=M5CoreS3') {
        'm5cores3_fmp3.name=M5CoreS3 (TOPPERS/FMP3)'
    }
    #  Core reference; see the note above.
    elseif ($_ -match '^m5cores3_fmp3\.build\.core=') {
        'm5cores3_fmp3.build.core=m5stack:esp32'
    }
    elseif ($_ -match '^m5cores3_fmp3\.build\.variant=') {
        'm5cores3_fmp3.build.variant=m5stack:m5stack_cores3'
    }
    else {
        $_
    }
}
$globalMenuLines = @($sourceBoardLines | Where-Object { $_ -match '^menu\.' })
$boardDefinitionLines = @(
    $sourceBoardLines | Where-Object { $_ -match '^m5cores3_fmp3\.' })
$boardLines = $globalMenuLines + @(
    'menu.FMP3Runtime=FMP3 Runtime'
) + $boardDefinitionLines + @(
    #  Which chip's prebuilt stages this board links against. A second board
    #  (plain M5Core, ESP32 LX6) sets esp32 here, so the stage layout is
    #  fmp3-prebuilt/<chip>/<profile> rather than fmp3-prebuilt/<profile>.
    #  Getting this in before publication avoids moving a path users' installs
    #  already depend on.
    "m5cores3_fmp3.build.toppers_chip=$Chip",
    #  Five runtime profiles were reduced to three.
    #
    #  'dual-core' is gone: it differed from m5-unified only in FMP3_PRC_NUM,
    #  so m5-unified now builds the SMP kernel and covers both.
    #
    #  'wifi-scan' is gone too. Once the two Wi-Fi adapters shared
    #  one bring-up (D-4d), the scan adapter could go into wifi-connect, so a
    #  separate scan-only profile bought nothing. The earlier attempt to fold it
    #  in was withdrawn because the WiFiScan example merely LINKED there - it
    #  bound to a weak stub returning ScanFailed. It really scans now.
    #
    #  The surviving option keys are unchanged so that sketches with a saved
    #  board selection keep working. 'dual' and 'wifi' are not reused for
    #  anything else - an old selection fails to resolve rather than silently
    #  building something different.
    'm5cores3_fmp3.menu.FMP3Runtime.minimal=Minimal',
    'm5cores3_fmp3.menu.FMP3Runtime.minimal.build.toppers_profile=minimal',
    'm5cores3_fmp3.menu.FMP3Runtime.m5=M5Unified + Dual Core',
    'm5cores3_fmp3.menu.FMP3Runtime.m5.build.toppers_profile=m5-unified',
    'm5cores3_fmp3.menu.FMP3Runtime.wificonnect=WiFi',
    'm5cores3_fmp3.menu.FMP3Runtime.wificonnect.build.toppers_profile=wifi-connect'
) + $(
    #  EXPERIMENTAL: M5Unified + SMP + Wi-Fi in one runtime. Only
    #  offered when its stage is actually present, so a normal install of the
    #  three shipped profiles does not show a menu entry that cannot build.
    #  Guarded because -PrebuiltStageRoot defaults to empty for the legacy
    #  path below, and Join-Path rejects an empty -Path outright: without this
    #  the installer could not run at all without a stage root, which is how
    #  Test-ArduinoReleasePackage.ps1 calls it. No stage root, no stage.
    if ((-not [string]::IsNullOrWhiteSpace($PrebuiltStageRoot)) -and
            (Test-Path -LiteralPath (Join-Path $PrebuiltStageRoot 'all-in-one'))) {
        @('m5cores3_fmp3.menu.FMP3Runtime.aio=All-in-one (experimental)',
          'm5cores3_fmp3.menu.FMP3Runtime.aio.build.toppers_profile=all-in-one')
    }
)
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllLines(
    (Join-Path $platformRoot 'boards.txt'),
    [string[]]$boardLines,
    $utf8NoBom)

if ([string]::IsNullOrWhiteSpace($PrebuiltStageRoot)) {
    #  Legacy path: build FMP3 during the sketch build (needs CMake/Ninja/Python
    #  on the user's machine).
    $recipeBase = 'powershell.exe -NoProfile -ExecutionPolicy Bypass ' +
        "-File `"$recipe`" " +
        '-ArduinoBuildPath "{build.path}" ' +
        '-ProjectName "{build.project_name}" ' +
        "-LibraryRoot `"$LibraryRoot`" " +
        "-CoreVersion `"$CoreVersion`" " +
        '-Profile "{build.toppers_profile}"'
    $linkRecipe = "$recipeBase -Mode Link"
    $objcopyRecipe = "$recipeBase -Mode Objcopy"
    $partitionsRecipe = ''
}
else {
    #  Everything the recipe needs goes inside the platform, so the recipe can be
    #  written with Arduino variables only.
    $platformTools = Join-Path $platformRoot 'fmp3-tools'
    $platformStages = Join-Path $platformRoot 'fmp3-prebuilt'
    [void](New-Item -ItemType Directory -Path $platformTools -Force)
    Copy-Item -LiteralPath $driverSource -Destination $platformTools -Force
    foreach ($stage in @(Get-ChildItem -LiteralPath $PrebuiltStageRoot -Directory)) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage.FullName 'link-manifest.json'))) {
            continue
        }
        Copy-Item -LiteralPath $stage.FullName `
            -Destination (Join-Path (Join-Path $platformStages $Chip) $stage.Name) `
            -Recurse -Force
    }
    $stagedProfiles = @(Get-ChildItem `
        -LiteralPath (Join-Path $platformStages $Chip) -Directory `
        -ErrorAction SilentlyContinue)
    if ($stagedProfiles.Count -eq 0) {
        throw "No prebuilt stage was found below $PrebuiltStageRoot"
    }

    $driverPrefix = "`"$PythonExecutable`" " +
        '"{runtime.platform.path}/fmp3-tools/fmp3_link.py"'
    $recipeBase = "$driverPrefix " +
        '--stage "{runtime.platform.path}/fmp3-prebuilt/{build.toppers_chip}/{build.toppers_profile}" ' +
        '--build-path "{build.path}" ' +
        '--project-name "{build.project_name}" ' +
        '--gcc "{compiler.path}{compiler.c.cmd}" ' +
        '--esptool "{tools.esptool_py.path}/{tools.esptool_py.cmd}" ' +
        '--sdk-ld "{compiler.sdk.path}/ld" ' +
        '--sdk-lib "{compiler.sdk.path}/lib"'
    $linkRecipe = $recipeBase
    $objcopyRecipe = "$recipeBase --check-only"
    #  The inherited recipe runs "python3 gen_esp32part.py" on every host except
    #  Windows, which puts a Python requirement back on macOS and Linux after
    #  the driver was frozen to remove it. The driver does the conversion
    #  itself; scripts/Test-PartitionTable.ps1 checks it against the original.
    $partitionsRecipe = "$driverPrefix --partitions " +
        '"{build.path}/partitions.csv" ' +
        '"{build.path}/{build.project_name}.partitions.bin"'
}

$platformLines = Get-Content -LiteralPath $sourcePlatformFile -Encoding utf8 |
    ForEach-Object {
        if ($_ -match '^name=') {
            'name=M5Stack Arduino with TOPPERS/FMP3'
        }
        elseif ($_ -match '^recipe\.c\.combine\.pattern=') {
            "recipe.c.combine.pattern=$linkRecipe"
        }
        elseif ($_ -match '^recipe\.objcopy\.bin\.pattern=') {
            "recipe.objcopy.bin.pattern=$objcopyRecipe"
        }
        elseif ($_ -match '^recipe\.objcopy\.partitions\.bin\.pattern=' -and
                -not [string]::IsNullOrWhiteSpace($partitionsRecipe)) {
            "recipe.objcopy.partitions.bin.pattern=$partitionsRecipe"
        }
        elseif ($_ -match '^recipe\.size\.regex=') {
            #  The inherited regex matches ESP-IDF section names, which the FMP3
            #  link does not produce, so the IDE reported 0 bytes used. These are
            #  the sections of ports/.../runtime/ld.
            'recipe.size.regex=^(?:\.iram_boot|\.flash_text|\.flash_rodata)' +
                '\s+([0-9]+).*'
        }
        elseif ($_ -match '^recipe\.size\.regex\.data=') {
            'recipe.size.regex.data=^(?:\.data|\.bss|\.kernel_bss' +
                '|\.diag_noinit)\s+([0-9]+).*'
        }
        else {
            $_
        }
    }
#  With prebuilt stages gen_esp32part.exe is not shipped, so no recipe may
#  depend on it. This catches the override silently ceasing to apply, which
#  would otherwise surface as a missing file during a sketch build on Windows.
#  It checks generated content rather than an argument, so it can only fail on a
#  code change here - arguments are validated before anything is removed.
if (-not [string]::IsNullOrWhiteSpace($partitionsRecipe)) {
    $danglingRecipes = @($platformLines | Where-Object {
        $_ -match '^recipe\..*\{tools\.gen_esp32part\.cmd\}'
    })
    if ($danglingRecipes.Count -gt 0) {
        throw ("A recipe still uses gen_esp32part, which this platform does " +
            "not ship: " + ($danglingRecipes -join '; '))
    }
}

[System.IO.File]::WriteAllLines(
    (Join-Path $platformRoot 'platform.txt'),
    [string[]]$platformLines,
    $utf8NoBom)
Copy-Item -LiteralPath (Join-Path $sourcePlatform 'programmers.txt') `
    -Destination (Join-Path $platformRoot 'programmers.txt')

[ordered]@{
    package = 'ToppersFMP3-M5CoreS3'
    installedAt = (Get-Date).ToString('o')
    libraryRoot = $LibraryRoot
    sourcePlatform = $sourcePlatform
    coreVersion = $CoreVersion
} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding utf8

Write-Host ''
Write-Host 'TOPPERS/FMP3 Arduino board platform installed.'
Write-Host "  Platform: $platformRoot"
Write-Host '  Board:    M5CoreS3 (TOPPERS/FMP3)'
Write-Host 'Restart Arduino IDE before selecting the board.'
