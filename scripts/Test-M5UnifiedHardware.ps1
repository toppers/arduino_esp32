<#
.SYNOPSIS
    Uploads the packaged M5Unified example and validates its serial result.

.DESCRIPTION
    Judged on what the SHIPPING image prints. It used to require
        [M5] board=... display=320x240 pmic=4 battery_mV=...
        [M5] 60-second M5Unified integration PASS
    which only fmp_app/phase5/phase5_m5_selftest.c emits. The stages that go
    into the Boards Manager package are built WITHOUT -SelfTest (see
    New-Fmp3PrebuiltStages.ps1), and this script flashes the .bin that
    Test-ArduinoReleasePackage.ps1 produced from that package, so those
    markers could never appear. The first required marker,
    "M5.begin and initial LCD draw PASS", does come from the shipping example
    itself and is kept.

    The shipping example reads board type, PMIC and battery into its own
    variables but does not print them; asserting on those needs a self-test
    image, which is a different thing to flash and belongs in its own test.
#>

[CmdletBinding()]
param(
    #  Which board is on -Port. The stage platform carries all three, and
    #  the m5 runtime is offered on the two with a display; the M5StickS3 is
    #  absent because m5-unified does not work there
    #  (docs/m5sticks3-m5unified.md).
    [ValidateSet('m5cores3_fmp3', 'm5core_fmp3')]
    [string]$Board = 'm5cores3_fmp3',
    [string]$Port = 'COM4',
    [int]$Baud = 115200,
    [int]$CaptureSeconds = 80,
    #  One says loop() ran at all; several say it keeps running. The bridge
    #  prints one every 1000 loop() calls.
    [int]$RequiredHeartbeats = 3,
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
    #  The image the Boards Manager package produces, built by
    #  Test-StagePlatform.ps1 from the platform install_platform.py
    #  assembles. It used to be the .bin Test-ArduinoReleasePackage.ps1
    #  built from the legacy library ZIP; the ZIP is CoreS3-only and is
    #  being retired, and the artifact that carries every board is the
    #  platform. The m5 runtime's image prints both processors and
    #  M5.begin's verdict, so this test and its sibling read the same
    #  shipped image and assert different halves of it.
    #
    #  Comments cannot sit inside a backtick continuation: putting them
    #  between the two halves of the Join-Path below broke the continuation
    #  and the call lost its -ChildPath entirely.
    $ApplicationBin = Join-Path $M5ArduinoRoot (Join-Path "build\stage-platform" ("{0}-m5\M5Unified.ino.bin" -f $Board))
}

$esptool = Join-Path $M5StackPackage 'tools\esptool_py\5.2.0\esptool.exe'
foreach ($required in @($esptool, $ApplicationBin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required path was not found: $required"
    }
}

#  The chip follows the board, and esptool's flash geometry follows the chip:
#  the CoreS3 is 16MB at 80MHz, the M5Core 4MB at 40MHz.
$chip = if ($Board -eq 'm5core_fmp3') { 'esp32' } else { 'esp32s3' }
$flashSize = if ($chip -eq 'esp32') { '4MB' } else { '16MB' }
$flashFreq = if ($chip -eq 'esp32') { '40m' } else { '80m' }
& $esptool --chip $chip --port $Port --baud 921600 `
    write-flash --flash-size $flashSize --flash-mode dio --flash-freq $flashFreq `
    0x10000 $ApplicationBin
if ($LASTEXITCODE -ne 0) {
    throw "Uploading the packaged M5Unified image failed (exit=$LASTEXITCODE)."
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
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            $log += $chunk
            Write-Host -NoNewline $chunk
        }
        #  The shipping image has no end state to wait for - it loops
        #  forever - so stop once M5.begin has reported and loop() has been
        #  seen running, rather than on a 60-second verdict line.
        if (($log -match '\[M5\] M5.begin (and|or) initial LCD draw (PASS|FAILED)') -and
                (([regex]::Matches($log, '\[Arduino\] loop heartbeat')).Count `
                    -ge $RequiredHeartbeats)) {
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

#  'or initial LCD draw FAILED' is the shipping example's own negative verdict
#  (examples/M5Unified/M5Unified.ino), so it belongs here rather than among the
#  required markers.
if ($log -match 'Guru Meditation|panic.ed|M5.begin or initial LCD draw FAILED') {
    throw 'The packaged M5Unified hardware log contains a panic or failure.'
}
foreach ($requiredPattern in @(
        '\[M5\] M5.begin and initial LCD draw PASS',
        '\[Arduino\] task=\d+ processor=1',
        '\[Arduino\] setup complete')) {
    if ($log -notmatch $requiredPattern) {
        throw "Required M5Unified hardware marker was not received: $requiredPattern"
    }
}
#  Counted, not just matched: one heartbeat says loop() ran, several say it
#  keeps running.
$heartbeats = ([regex]::Matches($log, '\[Arduino\] loop heartbeat')).Count
if ($heartbeats -lt $RequiredHeartbeats) {
    throw ("The Arduino loop did not keep running: $heartbeats heartbeat(s), " +
        "needed $RequiredHeartbeats.")
}

Write-Host ''
Write-Host 'Packaged M5Unified COM hardware probe passed.'
Write-Host ('  M5.begin and initial LCD draw PASS')
Write-Host ('  Arduino task on PRC1, {0} loop heartbeat(s)' -f $heartbeats)
