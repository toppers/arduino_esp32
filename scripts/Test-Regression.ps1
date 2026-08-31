<#
.SYNOPSIS
    Runs the reproducible host-side regression suite.

.DESCRIPTION
    By default every Arduino-facing phase is rebuilt with the installed core.
    Use -ReuseExistingArtifacts for a faster check after a known full build.
    Hardware upload and serial validation are intentionally handled by
    Test-Hardware.ps1.
#>

[CmdletBinding()]
param(
    [string]$M5ArduinoRoot = '',
    [switch]$ReuseExistingArtifacts
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$tests = @(
    @{
        Name = 'Arduino library'
        Script = 'Test-ArduinoLibrary.ps1'
        Arguments = @()
    },
    @{
        Name = 'recipe override'
        Script = 'Test-RecipeOverride.ps1'
        Arguments = @()
    },
    @{
        Name = 'sketch bridge'
        Script = 'Test-SketchBridge.ps1'
        Arguments = @()
    },
    @{
        Name = 'API boundary and M5Unified link'
        Script = 'Test-M5UnifiedLink.ps1'
        Arguments = if ($ReuseExistingArtifacts) { @('-ReuseArduinoObjects') } else { @() }
    },
    @{
        Name = 'M5Unified integration'
        Script = 'Test-M5Unified.ps1'
        Arguments = if ($ReuseExistingArtifacts) { @('-ReuseArduinoObjects') } else { @() }
    },
    @{
        Name = 'SMP'
        Script = 'Test-Smp.ps1'
        Arguments = if ($ReuseExistingArtifacts) { @('-ReuseArduinoObjects') } else { @() }
    },
    @{
        Name = 'credential-free Wi-Fi scan'
        Script = 'Test-WiFiScan.ps1'
        Arguments = if ($ReuseExistingArtifacts) { @('-SkipBuild') } else { @() }
    }
)

$results = [System.Collections.Generic.List[object]]::new()
$started = Get-Date

foreach ($test in $tests) {
    $script = Join-Path $M5ArduinoRoot "scripts\$($test.Script)"
    if (-not (Test-Path -LiteralPath $script)) {
        throw "Regression script was not found: $script"
    }

    Write-Host ''
    Write-Host ('=== {0} ===' -f $test.Name)
    $testStarted = Get-Date
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $script @($test.Arguments)
    $exitCode = $LASTEXITCODE
    $elapsed = [math]::Round(((Get-Date) - $testStarted).TotalSeconds, 1)
    $results.Add([pscustomobject]@{
        Test = $test.Name
        Result = if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' }
        Seconds = $elapsed
    })
    if ($exitCode -ne 0) {
        $results | Format-Table -AutoSize
        throw "$($test.Name) failed (exit=$exitCode)."
    }
}

Write-Host ''
Write-Host 'Host-side regression summary'
$results | Format-Table -AutoSize
Write-Host ('Total: {0:N1} seconds' -f ((Get-Date) - $started).TotalSeconds)
Write-Host 'QEMU, visual LCD/touch checks, and credentialed Wi-Fi are not part of this run.'
