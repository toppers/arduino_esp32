<#
.SYNOPSIS
    Compiles the library-recognition sketch for M5Stack CoreS3.

.DESCRIPTION
    Needs the TOPPERS/FMP3 board platform installed in -Sketchbook, because
    examples/LibraryInfo writes to the kernel's own log port
    (target_fput_log) rather than to the M5Stack core's Serial. On the stock
    m5stack FQBN this test used to build, there is no FMP3 runtime to resolve
    that against, so the link could not succeed:

        LibraryInfo.ino:19: undefined reference to `target_fput_log'

    scripts/verify_package.py builds the same example on toppers:esp32 for the
    same reason. Install the platform first:

        python scripts/build_prebuilt_stages.py --chip esp32s3
        python scripts/install_platform.py --prebuilt-stage-root build/prebuilt
#>

[CmdletBinding()]
param(
    [string]$ArduinoCli = (@(
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Join-Path ${env:ProgramFiles} 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        (Get-Command 'arduino-cli' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1),
    [string]$M5StackPackage = (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$LibraryRoot = '',
    [string]$BuildDirectory = '',

    #  Where the TOPPERS/FMP3 platform is installed; install_platform.py
    #  defaults to the same place.
    [string]$Sketchbook = '',

    #  -Chip picks this port's board FQBN and the toolchain's name; -Fqbn
    #  overrides the former. install_platform.py names the boards.
    [ValidateSet('esp32s3', 'esp32')]
    [string]$Chip = 'esp32s3',

    [string]$Fqbn = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $LibraryRoot `
        ("build\arduino-phase1" + $(if ($Chip -eq 'esp32s3') { '' } else { "-$Chip" }))
}
if ([string]::IsNullOrWhiteSpace($Fqbn)) {
    $Fqbn = switch ($Chip) {
        'esp32' { 'toppers:esp32:m5core_fmp3' }
        default { 'toppers:esp32:m5cores3_fmp3' }
    }
}
if ([string]::IsNullOrWhiteSpace($Sketchbook)) {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if ([string]::IsNullOrWhiteSpace($documents)) {
        throw 'Documents folder is unavailable; specify -Sketchbook.'
    }
    $Sketchbook = Join-Path $documents 'Arduino'
}

$sketch = Join-Path $LibraryRoot 'examples\LibraryInfo'
$nm = Join-Path $M5StackPackage `
    "tools\esp-x32\2601\bin\xtensa-$Chip-elf-nm.exe"

foreach ($required in @($ArduinoCli, $sketch, $nm)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

#  Said here rather than left to arduino-cli, whose message for an unknown
#  FQBN does not mention that a platform has to be installed at all.
$platformBoards = Join-Path $Sketchbook 'hardware\toppers\esp32\boards.txt'
if ($Fqbn.StartsWith('toppers:', [System.StringComparison]::Ordinal) -and
        -not (Test-Path -LiteralPath $platformBoards)) {
    throw ("The TOPPERS/FMP3 platform is not installed in $Sketchbook. Run " +
        'scripts/install_platform.py --prebuilt-stage-root build/prebuilt ' +
        'first, or pass -Sketchbook.')
}

#  So that -Sketchbook actually governs where the platform is looked up.
#  Without this arduino-cli uses its own default user directory and the
#  parameter would only be checked above, not honoured here.
$originalUserDir = $env:ARDUINO_DIRECTORIES_USER
try {
    $env:ARDUINO_DIRECTORIES_USER = $Sketchbook
    & $ArduinoCli compile `
        --fqbn $Fqbn `
        --library $LibraryRoot `
        --build-path $BuildDirectory `
        $sketch
    $compileExit = $LASTEXITCODE
}
finally {
    $env:ARDUINO_DIRECTORIES_USER = $originalUserDir
}
if ($compileExit -ne 0) {
    throw "Arduino compile failed (exit=$compileExit)"
}

$elf = Get-ChildItem -LiteralPath $BuildDirectory -File -Filter '*.elf' |
    Select-Object -First 1
if (-not $elf) {
    throw "Arduino ELF was not generated in $BuildDirectory"
}

$symbol = @(& $nm -C $elf.FullName |
    Select-String 'toppers::fmp3::m5cores3::libraryInfo\(\)')
if ($LASTEXITCODE -ne 0 -or $symbol.Count -eq 0) {
    throw 'The public library implementation was not linked into the ELF.'
}

Write-Host ''
Write-Host 'Arduino library compile passed.'
Write-Host ('  FQBN: {0}' -f $Fqbn)
Write-Host ('  Symbol: {0}' -f $symbol[0].Line.Trim())
Get-FileHash -Algorithm SHA256 $elf.FullName |
    ForEach-Object { Write-Host ('  ELF SHA-256: {0}' -f $_.Hash) }
Get-ChildItem -LiteralPath $BuildDirectory -File -Filter '*.ino.bin' |
    ForEach-Object {
        Get-FileHash -Algorithm SHA256 $_.FullName |
            ForEach-Object { Write-Host ('  BIN SHA-256: {0}' -f $_.Hash) }
    }
