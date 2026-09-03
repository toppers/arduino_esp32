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

Write-Host ('Flashing {0}. Do NOT touch yet - this takes a moment, and the ' +
    'window has not opened.' -f $Port)
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
#  Say when the window opens, and remember it.
#
#  This test needs a person, and the only line it used to print - "After the
#  touch screen appears, touch several locations on the LCD." - came out
#  BEFORE the flash, tens of seconds before anything could be registered. On
#  2026-09-02 the screen was touched outside the window, the run reported
#  touches=0, and that was read as the board's touch being broken. It was not:
#  the next run passed. A human's "yes I touched it" cannot distinguish the
#  two, so the test has to say when, and say afterwards what interval it was
#  actually watching.
$windowOpened = Get-Date
Write-Host ''
Write-Host ('=== TOUCH NOW === window open {0:HH:mm:ss}, closes {1:HH:mm:ss} ({2}s)' -f
    $windowOpened, $windowOpened.AddSeconds($CaptureSeconds), $CaptureSeconds)
Write-Host 'Press several places on the LCD, every second or two, until told to stop.'
try {
    $serial.Open()
    #  Reset the board AFTER the port is open, so the capture starts at boot.
    #  The banner and the bridge's one-shot lines (Processor N start.,
    #  [Arduino] task=N processor=M) are printed within milliseconds of reset,
    #  and esptool's own "Hard resetting via RTS pin" happens before this
    #  script can open the port - Test-DualCoreHardware.ps1 was missing the
    #  whole banner because of it and only saw the heartbeats.
    #
    #  A clean EN pulse: DTR low, RTS high (EN low), hold, RTS low (EN
    #  released). The previous sequence drove DTR and RTS true at the same
    #  time, which asserts neither EN nor IO0 on the ESP32 auto-reset circuit
    #  (nor on the ESP32-S3's USB-Serial/JTAG), so it did not reliably reset
    #  anything. 300 ms because 100 ms was not always enough for the S3.
    $serial.DtrEnable = $false
    $serial.RtsEnable = $true
    Start-Sleep -Milliseconds 300
    $serial.RtsEnable = $false

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
    #  Name the interval that was watched, so a failure can be told apart from
    #  having touched outside it. "touches=0" over a full window of polling is
    #  the board; touching at the wrong time is not.
    $updates = [regex]::Matches($log, '\[M5\] alive \d+s updates=(\d+) touches=(\d+)')
    $polled = if ($updates.Count -gt 0) {
        $updates[$updates.Count - 1].Groups[1].Value
    } else { 'unknown' }
    throw ('No touch coordinate arrived in the window {0:HH:mm:ss}-{1:HH:mm:ss} ' +
        '({2}s). The image polled {3} times in it. If the screen was pressed ' +
        'outside that interval this says nothing about the hardware - run it ' +
        'again and press only after the TOUCH NOW line.' -f
        $windowOpened, $windowOpened.AddSeconds($CaptureSeconds),
        $CaptureSeconds, $polled)
}
if ($log -notmatch '\[M5\] 60-second M5Unified integration PASS') {
    throw 'The 60-second M5Unified PASS marker was not received.'
}

Write-Host ''
Write-Host 'CoreS3 touch probe passed.'
Write-Host ('  First touch: x={0}, y={1}' -f
    $touch.Groups[1].Value, $touch.Groups[2].Value)
