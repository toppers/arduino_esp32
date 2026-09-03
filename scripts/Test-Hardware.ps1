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
        $log += $serial.ReadExisting()
        #  Tail only; see the verdict check below for why the prefix cannot
        #  be relied on. With the prefix here the loop never broke early and
        #  every run waited out the full -CaptureSeconds.
        if ($log -match 'API boundary probe (PASS|FAILED)') {
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
#  Without the "[APIProbe] " prefix, to match the negative check above.
#  Both processors write to the same serial port, so lines interleave and
#  characters are lost - a capture that clearly contained the verdict still
#  failed an exact-line match, because the prefix had been clobbered:
#      5_shim: queue pool exhausted (need >4)
#      obe] FreeRTOS API boundary probe PASS
#      RTOS API boundary probe PASS
#  The tail of a line survives; its start is what another writer overwrites,
#  and how much it eats varies per run - the two captures above are the same
#  verdict from two runs. So match the tail only, exactly as the negative
#  check does. Anchoring on any part of the prefix is unreliable by
#  construction, not flaky.
if ($log -notmatch 'API boundary probe PASS') {
    throw 'The PASS marker was not received before the timeout.'
}

Write-Host ''
Write-Host 'COM hardware probe passed.'
Write-Host '  Queue empty/full/FIFO/reset: PASS'
Write-Host '  FromISR compatibility wrappers (task-context invocation): PASS'
Write-Host '  Semaphore and queue pool exhaustion/reuse: PASS'
