<#
.SYNOPSIS
    Uploads the API probe to CoreS3 and validates its serial result.
#>

[CmdletBinding()]
param(
    [string]$Port = 'COM4',
    [int]$Baud = 115200,
    [int]$CaptureSeconds = 35,
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
        'build\arduino-phase4-m5unified\M5UnifiedLink.ino.bin'
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
    throw "Uploading the API probe failed (exit=$LASTEXITCODE)."
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
        $log += $serial.ReadExisting()
        if ($log -match '\[APIProbe\] FreeRTOS API boundary probe (PASS|FAILED)') {
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

Write-Host $log
if ($log -match 'Guru Meditation|panic.ed|API boundary probe FAILED') {
    throw 'The hardware log contains a panic or failed probe.'
}
if ($log -notmatch '\[APIProbe\] FreeRTOS API boundary probe PASS') {
    throw 'The PASS marker was not received before the timeout.'
}

Write-Host ''
Write-Host 'COM hardware probe passed.'
Write-Host '  Queue empty/full/FIFO/reset: PASS'
Write-Host '  FromISR compatibility wrappers (task-context invocation): PASS'
Write-Host '  Semaphore and queue pool exhaustion/reuse: PASS'
