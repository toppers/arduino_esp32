<#
.SYNOPSIS
    Checks the driver's partition table conversion against gen_esp32part.

.DESCRIPTION
    The platform inherits tools.gen_esp32part.cmd from the
    M5Stack core, and outside Windows that runs "python3 gen_esp32part.py". That
    put a Python requirement back on macOS and Linux, where python3 is not
    always installed, so scripts/fmp3_link.py --partitions does the conversion
    and the recipe points at the frozen driver on every host.

    Being a reimplementation, it has to agree with the original. This compares
    the two byte for byte over every partition CSV the platform ships, and
    checks that malformed tables are rejected by both.

    Windows only: gen_esp32part.exe is the reference, and it is the Windows
    build. That is fine - the point is to pin the conversion down on the machine
    where the reference exists.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
      -File .\scripts\Test-PartitionTable.ps1 `
      -PythonExecutable '<path to python.exe>'
#>

[CmdletBinding()]
param(
    [string]$LibraryRoot = '',
    [string]$PythonExecutable = '',

    #  Where gen_esp32part.exe and partitions/ live. Defaults to the installed
    #  FMP3 platform, falling back to the M5Stack core.
    [string]$ToolsDirectory = '',
    [string]$ArduinoData = '',
    [string]$CoreVersion = '3.3.8',

    [string]$WorkDirectory = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($LibraryRoot)) {
    $LibraryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
    $found = Get-Command 'python.exe' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $found) {
        throw 'Python was not found. Pass -PythonExecutable.'
    }
    $PythonExecutable = $found.Source
}
if ([string]::IsNullOrWhiteSpace($ArduinoData)) {
    $ArduinoData = Join-Path $env:LOCALAPPDATA 'Arduino15'
}
if ([string]::IsNullOrWhiteSpace($ToolsDirectory)) {
    $candidates = @(
        (Join-Path ([Environment]::GetFolderPath('MyDocuments')) `
            'Arduino\hardware\toppers\esp32\tools'),
        (Join-Path $ArduinoData "packages\m5stack\hardware\esp32\$CoreVersion\tools"))
    $ToolsDirectory = $candidates |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'gen_esp32part.exe') } |
        Select-Object -First 1
    if ($null -eq $ToolsDirectory) {
        throw ('gen_esp32part.exe was not found. Looked in: ' +
            ($candidates -join '; '))
    }
}
if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
    $WorkDirectory = Join-Path $env:TEMP 'fmp3-partition-test'
}

$reference = Join-Path $ToolsDirectory 'gen_esp32part.exe'
$partitions = Join-Path $ToolsDirectory 'partitions'
$driver = Join-Path $LibraryRoot 'scripts\fmp3_link.py'
foreach ($required in @($reference, $partitions, $driver)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Input was not found: $required"
    }
}

if (Test-Path -LiteralPath $WorkDirectory) {
    Remove-Item -LiteralPath $WorkDirectory -Recurse -Force
}
[void](New-Item -ItemType Directory -Path $WorkDirectory -Force)

# Native commands write progress to stderr, which $ErrorActionPreference =
# 'Stop' turns into a terminating error. Judge them by their exit code.
function Invoke-Converter {
    param([string]$FilePath, [string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $FilePath @Arguments 2>&1
        return [pscustomobject]@{ Code = $LASTEXITCODE; Output = ($output -join "`n") }
    }
    finally {
        $ErrorActionPreference = $previous
    }
}

function Convert-Both {
    param([string]$Csv, [string]$Name)
    $referenceBin = Join-Path $WorkDirectory "$Name.reference.bin"
    $driverBin = Join-Path $WorkDirectory "$Name.driver.bin"
    return [pscustomobject]@{
        Name = $Name
        Reference = (Invoke-Converter -FilePath $reference `
            -Arguments @('-q', $Csv, $referenceBin))
        Driver = (Invoke-Converter -FilePath $PythonExecutable `
            -Arguments @($driver, '--partitions', $Csv, $driverBin))
        ReferenceBin = $referenceBin
        DriverBin = $driverBin
    }
}

$identical = 0
$failures = [System.Collections.Generic.List[string]]::new()

#  The FMP3 platform no longer ships gen_esp32part.exe, so the reference
#  normally resolves to the M5Stack core. Say which one was used rather
#  than leaving the fallback silent.
Write-Host "Reference: $reference"
Write-Host 'Comparing every partition CSV the platform ships'
foreach ($csv in @(Get-ChildItem -LiteralPath $partitions -Filter '*.csv' -File |
        Sort-Object Name)) {
    $result = Convert-Both -Csv $csv.FullName -Name $csv.BaseName
    if ($result.Reference.Code -ne 0 -or $result.Driver.Code -ne 0) {
        $failures.Add(("$($csv.Name): reference=$($result.Reference.Code) " +
            "driver=$($result.Driver.Code)`n  $($result.Reference.Output)" +
            "`n  $($result.Driver.Output)"))
        continue
    }
    $a = [System.IO.File]::ReadAllBytes($result.ReferenceBin)
    $b = [System.IO.File]::ReadAllBytes($result.DriverBin)
    if ([System.Linq.Enumerable]::SequenceEqual($a, $b)) {
        $identical++
    }
    else {
        $failures.Add("$($csv.Name): output differs")
    }
}
Write-Host "  byte identical: $identical"

#  A reimplementation that accepts a broken table would be worse than one that
#  differs on a good one, so check that both reject the same malformed inputs.
$malformed = [ordered]@{
    'duplicate-names' = @(
        'nvs,      data, nvs,     0x9000,  0x5000,',
        'nvs,      data, nvs,     0xe000,  0x5000,')
    'overlapping' = @(
        'app0,     app,  ota_0,   0x10000, 0x140000,',
        'app1,     app,  ota_1,   0x20000, 0x140000,')
    'unaligned-app-offset' = @(
        'app0,     app,  ota_0,   0x9000,  0x140000,')
    'small-rw-nvs' = @(
        'nvs,      data, nvs,     0x9000,  0x1000,')
    'unknown-flag' = @(
        'nvs,      data, nvs,     0x9000,  0x5000, nosuchflag')
    'empty-size' = @(
        'nvs,      data, nvs,     0x9000,')
    'below-table' = @(
        'nvs,      data, nvs,     0x1000,  0x5000,')
}

Write-Host 'Checking that malformed tables are rejected by both'
foreach ($case in $malformed.GetEnumerator()) {
    $csv = Join-Path $WorkDirectory "$($case.Key).csv"
    [System.IO.File]::WriteAllLines($csv,
        [string[]](@('# Name,   Type, SubType, Offset,  Size, Flags') + $case.Value))
    $result = Convert-Both -Csv $csv -Name $case.Key
    $referenceRejected = $result.Reference.Code -ne 0
    $driverRejected = $result.Driver.Code -ne 0
    if ($referenceRejected -and $driverRejected) {
        Write-Host "  $($case.Key): both rejected"
    }
    elseif (-not $referenceRejected -and -not $driverRejected) {
        #  Not a failure of the driver: the reference accepts it too. Recorded
        #  so the case is not mistaken for coverage it does not provide.
        Write-Host "  $($case.Key): both accepted (not actually malformed)"
    }
    else {
        $failures.Add(("$($case.Key): only one rejected " +
            "(reference=$($result.Reference.Code) driver=$($result.Driver.Code))" +
            "`n  $($result.Reference.Output)`n  $($result.Driver.Output)"))
    }
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host "FAILED ($($failures.Count))"
    foreach ($failure in $failures) {
        Write-Host "  $failure"
    }
    throw "Partition table conversion does not match gen_esp32part."
}
Write-Host "PASSED: $identical CSVs byte identical, malformed inputs agree."
Write-Host "Work directory: $WorkDirectory"
#  The malformed cases leave a non-zero $LASTEXITCODE behind, which would
#  otherwise become this script exit code even though the test passed.
exit 0
