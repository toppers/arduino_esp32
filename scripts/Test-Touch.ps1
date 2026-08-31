<#
.SYNOPSIS
    Uploads the touch image and validates a user touch on CoreS3.
#>

[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$Baud = 115200,
    [int]$CaptureSeconds = 75,
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
        'build\arduino-phase5-m5unified\M5Unified.ino.bin'
}

$esptool = Join-Path $M5StackPackage 'tools\esptool_py\5.2.0\esptool.exe'
foreach ($required in @($esptool, $ApplicationBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

Write-Host 'After the touch screen appears, touch several locations on the LCD.'
& $esptool --chip esp32s3 --port $Port --baud 921600 `
    write-flash --flash-size 16MB --flash-mode dio --flash-freq 80m `
    0x10000 $ApplicationBin
if ($LASTEXITCODE -ne 0) {
    throw "Uploading the touch image failed (exit=$LASTEXITCODE)."
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
        if (($log -match '\[M5\] first touch x=-?\d+ y=-?\d+') -and
            ($log -match '\[M5\] 60-second M5Unified integration PASS')) {
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

if ($log -match 'Guru Meditation|panic.ed|M5Unified integration FAILED') {
    throw 'The touch-test log contains a panic or failed integration probe.'
}
$touch = [regex]::Match(
    $log, '\[M5\] first touch x=(-?\d+) y=(-?\d+)')
if (-not $touch.Success) {
    throw 'No touch coordinate was received before the timeout.'
}
if ($log -notmatch '\[M5\] 60-second M5Unified integration PASS') {
    throw 'The 60-second M5Unified PASS marker was not received.'
}

Write-Host ''
Write-Host 'CoreS3 touch probe passed.'
Write-Host ('  First touch: x={0}, y={1}' -f
    $touch.Groups[1].Value, $touch.Groups[2].Value)
