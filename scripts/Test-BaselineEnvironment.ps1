<#
.SYNOPSIS
    Verifies the local tools and source trees required by the M5Arduino baseline.

.DESCRIPTION
    This script is read-only. It checks the pinned ESP32-S3/FMP3 source tree,
    the installed M5Stack Arduino core, and the Windows build tools used by the
    initial investigation.
#>

[CmdletBinding()]
param(
    #  The source tree this port builds from. It used to be an EXTERNAL
    #  toppers/fmp3_esp_idf checkout, given because nothing could derive it.
    #  The runtime is vendored here now (ports/m5stack_xtensa/runtime plus the
    #  third_party/fmp3_core submodule) and nothing builds against an external
    #  tree any more, so this defaults to this repository and the check is
    #  about the submodule actually being checked out.
    [string]$SourceTree = '',
    [string]$M5StackPackage = (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$CMake = ((Get-Command 'cmake.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)),
    [string]$Ninja = ((Get-Command 'ninja.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)),
    [string]$ArduinoCli = (@(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Get-Command 'arduino-cli' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1)
)

$ErrorActionPreference = 'Stop'
$failures = [System.Collections.Generic.List[string]]::new()

if ([string]::IsNullOrWhiteSpace($SourceTree)) {
    $SourceTree = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Test-RequiredPath {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (Test-Path -LiteralPath $Path) {
        Write-Host ('[OK]   {0}: {1}' -f $Label, $Path)
        return $true
    }

    Write-Host ('[FAIL] {0}: {1}' -f $Label, $Path)
    $script:failures.Add(('{0}: {1}' -f $Label, $Path))
    return $false
}

function Invoke-VersionCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [Parameter(Mandatory)]
        [string]$Command,
        [string[]]$Arguments = @('--version')
    )

    if (-not (Test-Path -LiteralPath $Command)) {
        return
    }

    $firstLine = (& $Command @Arguments 2>&1 | Select-Object -First 1)
    Write-Host ('       {0}: {1}' -f $Label, $firstLine)
}

$arduinoCore = Join-Path $M5StackPackage 'hardware\esp32\3.3.8'
$toolchain = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin\xtensa-esp32s3-elf-gcc.exe'
$esptool = Join-Path $M5StackPackage 'tools\esptool_py\5.2.0\esptool.exe'
$boardFile = Join-Path $arduinoCore 'boards.txt'
$platformFile = Join-Path $arduinoCore 'platform.txt'

Write-Host 'M5Arduino baseline environment'
Write-Host '------------------------------'

$repoPresent = Test-RequiredPath 'FMP3 source tree' $SourceTree
Test-RequiredPath 'fmp3_core submodule' (
    Join-Path $SourceTree 'third_party\fmp3_core\CMakeLists.txt') | Out-Null
Test-RequiredPath 'vendored runtime' (
    Join-Path $SourceTree 'ports\m5stack_xtensa\runtime\CMakeLists.txt') | Out-Null
Test-RequiredPath 'M5Stack Arduino core 3.3.8' $arduinoCore | Out-Null
Test-RequiredPath 'M5CoreS3 boards.txt' $boardFile | Out-Null
Test-RequiredPath 'M5Stack platform.txt' $platformFile | Out-Null
Test-RequiredPath 'Xtensa ESP32-S3 compiler' $toolchain | Out-Null
Test-RequiredPath 'esptool' $esptool | Out-Null
Test-RequiredPath 'CMake' $CMake | Out-Null
Test-RequiredPath 'Ninja' $Ninja | Out-Null
Test-RequiredPath 'Arduino CLI' $ArduinoCli | Out-Null

Write-Host ''
Write-Host 'Versions'
Write-Host '--------'
Invoke-VersionCommand 'CMake' $CMake
Invoke-VersionCommand 'Ninja' $Ninja
Invoke-VersionCommand 'Xtensa GCC' $toolchain
Invoke-VersionCommand 'esptool' $esptool @('version')
Invoke-VersionCommand 'Arduino CLI' $ArduinoCli @('version')

if ($repoPresent) {
    Write-Host ''
    Write-Host 'FMP3 source state'
    Write-Host '-----------------'

    #  'Continue' for the duration: Windows PowerShell turns a native
    #  command's stderr into an ErrorRecord, and with 'Stop' a git failure
    #  killed this script before it printed the failure summary - the first
    #  run of this suite exited 1 without saying what had failed.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $commit = (& git -C $SourceTree rev-parse HEAD 2>&1)
        $gitExit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($gitExit -eq 0) {
        Write-Host ('Commit: {0}' -f $commit)
        & git -C $SourceTree status --short --branch
        Write-Host 'Submodule pins:'
        & git -C $SourceTree ls-tree -r HEAD |
            Select-String '^160000' |
            ForEach-Object { Write-Host ('  {0}' -f $_.Line) }
    }
    else {
        Write-Host ('[FAIL] Git state: {0}' -f ($commit -join [Environment]::NewLine))
        $failures.Add('Git state could not be read')
    }
}

if (Test-Path -LiteralPath $boardFile) {
    $coreS3 = Select-String -LiteralPath $boardFile -Pattern '^m5stack_cores3\.'
    if (-not $coreS3) {
        Write-Host '[FAIL] m5stack_cores3 board definition was not found'
        $failures.Add('m5stack_cores3 board definition')
    }
    else {
        Write-Host ''
        Write-Host ('[OK]   M5CoreS3 board definition: {0} entries' -f $coreS3.Count)
    }
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host ('Baseline environment check failed ({0} item(s)).' -f $failures.Count)
    exit 1
}

Write-Host 'Baseline environment check passed.'
