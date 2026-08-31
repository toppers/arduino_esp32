<#
.SYNOPSIS
    Compiles the library-recognition sketch for M5Stack CoreS3.
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
    [string]$Fqbn = 'm5stack:esp32:m5stack_cores3'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $LibraryRoot 'build\arduino-phase1'
}

$sketch = Join-Path $LibraryRoot 'examples\LibraryInfo'
$nm = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin\xtensa-esp32s3-elf-nm.exe'

foreach ($required in @($ArduinoCli, $sketch, $nm)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

& $ArduinoCli compile `
    --fqbn $Fqbn `
    --library $LibraryRoot `
    --build-path $BuildDirectory `
    $sketch
if ($LASTEXITCODE -ne 0) {
    throw "Arduino compile failed (exit=$LASTEXITCODE)"
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
