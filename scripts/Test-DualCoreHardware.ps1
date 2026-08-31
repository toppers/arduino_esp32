<#
.SYNOPSIS
    Uploads the packaged DualCore example and validates both processors.
#>

[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$Baud = 115200,
    [int]$CaptureSeconds = 20,
    [string]$M5ArduinoRoot = '',
    [string]$M5StackPackage =
        (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$ApplicationBin = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($ApplicationBin)) {
    $ApplicationBin = Join-Path $M5ArduinoRoot `
        'build\release\install-test\dual-core-build\DualCore.ino.bin'
}

$esptool = Join-Path $M5StackPackage 'tools\esptool_py\5.2.0\esptool.exe'
foreach ($required in @($esptool, $ApplicationBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

& $esptool --chip esp32s3 --port $Port --baud 921600 `
    write-flash --flash-size 16MB --flash-mode dio --flash-freq 80m `
    0x10000 $ApplicationBin
if ($LASTEXITCODE -ne 0) {
    throw "Uploading the packaged DualCore image failed (exit=$LASTEXITCODE)."
}

$serial = [System.IO.Ports.SerialPort]::new(
    $Port, $Baud, [System.IO.Ports.Parity]::None, 8,
    [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 250
$log = ''
try {
    $serial.Open()
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    Start-Sleep -Milliseconds 100
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    Start-Sleep -Milliseconds 100
    $serial.RtsEnable = $false
    Start-Sleep -Milliseconds 100
    $serial.DtrEnable = $false

    $until = (Get-Date).AddSeconds($CaptureSeconds)
    while ((Get-Date) -lt $until) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            $log += $chunk
            Write-Host -NoNewline $chunk
        }
        if ($log -match '\[SMP\] dual-core isolation (PASS|FAILED)') {
            Start-Sleep -Milliseconds 500
            $log += $serial.ReadExisting()
            break
        }
        Start-Sleep -Milliseconds 100
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}

if ($log -match 'Guru Meditation|panic.ed|dual-core isolation FAILED') {
    throw 'The packaged DualCore hardware log contains a panic or failure.'
}
foreach ($requiredPattern in @(
        '\[SMP\] Arduino loops=\d+ PRC2 iterations=\d+',
        '\[SMP\] dual-core isolation PASS')) {
    if ($log -notmatch $requiredPattern) {
        throw "Required DualCore hardware marker was not received: $requiredPattern"
    }
}

Write-Host ''
Write-Host 'Packaged DualCore COM hardware probe passed.'
