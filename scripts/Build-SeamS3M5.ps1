<#
.SYNOPSIS
    Builds the ESP32-S3/FMP3 M5Stack seam target on Windows.

.DESCRIPTION
    Configures the external esp32_s3 repository into M5Arduino/build, uses the
    standalone esptool.exe bundled with the M5Stack Arduino package, and makes
    the Xtensa binutils and Git for Windows Bash visible only for this process.
#>

[CmdletBinding()]
param(
    #  Checkout of the reference port. The reference is the
    #  PUBLIC toppers/fmp3_esp_idf; there is nothing to derive this from, so
    #  it has to be given. The old default named a path on another machine and
    #  the repository it named is no longer the reference.
    [string]$Fmp3Repository = '',
    [string]$BuildDirectory = '',
    [string]$M5StackPackage = (Join-Path $env:LOCALAPPDATA 'Arduino15\packages\m5stack'),
    [string]$CMake = ((Get-Command 'cmake.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)),
    [string]$Ninja = ((Get-Command 'ninja.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)),
    #  Built from ProgramFiles rather than written out. Get-Command bash.exe is
    #  NOT usable here: on Windows it usually finds the WSL launcher in
    #  System32 instead of Git Bash, and the script needs the Git one.
    [string]$GitBash = (Join-Path $env:ProgramFiles 'Git\bin\bash.exe'),
    [int]$Parallel = 8,
    [switch]$SkipRomLinkSetup,
    [string]$ExternalApplicationDirectory = '',
    [string]$ExternalApplicationName = '',
    [string]$ExternalObjects = '',
    [ValidateRange(1, 2)]
    [int]$ProcessorCount = 1,
    [ValidateSet('m5', 'wifi')]
    [string]$Variant = 'm5',
    [ValidateSet('wifi_sta', 'wifi_scan')]
    [string]$WifiApplication = 'wifi_sta'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $PSScriptRoot '..\build\baseline-seam-s3-m5'
}

function Assert-Path {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw ('{0} が見つかりません: {1}' -f $Label, $Path)
    }
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [Parameter(Mandatory)]
        [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw ('{0} に失敗しました (exit={1})' -f $Label, $LASTEXITCODE)
    }
}

$toolchainBin = Join-Path $M5StackPackage 'tools\esp-x32\2601\bin'
$toolchainFile = Join-Path $Fmp3Repository 'cmake\toolchain-xtensa-esp32s3.cmake'
$esptool = Join-Path $M5StackPackage 'tools\esptool_py\5.2.0\esptool.exe'
$nm = Join-Path $toolchainBin 'xtensa-esp32s3-elf-nm.exe'
$romLinkSetup = Join-Path $Fmp3Repository 'scripts\setup_wifi_ld_links.sh'
$driver = Join-Path $Fmp3Repository 'cmake\a1_xip_build.cmake'

Assert-Path 'ESP32-S3/FMP3 repository' $Fmp3Repository
Assert-Path 'CMake' $CMake
Assert-Path 'Ninja' $Ninja
Assert-Path 'Git Bash' $GitBash
Assert-Path 'Xtensa toolchain' $toolchainBin
Assert-Path 'Xtensa toolchain file' $toolchainFile
Assert-Path 'Arduino package esptool' $esptool
Assert-Path 'Xtensa nm' $nm
Assert-Path 'XIP build driver' $driver

if (-not (Select-String -LiteralPath (Join-Path $Fmp3Repository 'CMakeLists.txt') `
        -Pattern 'A1_ESPTOOL_EXECUTABLE' -Quiet)) {
    throw 'esp32_s3へ patches\esp32_s3-windows-host-tools.patch を適用してください。'
}

$requiredRomLd = @(
    'esp32s3.rom.ld',
    'esp32s3.rom.api.ld',
    'esp32s3.rom.libc.ld',
    'esp32s3.rom.libgcc.ld',
    'esp32s3.rom.newlib.ld',
    'esp32s3.rom.version.ld'
)
$missingRomLd = @($requiredRomLd | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $Fmp3Repository "esp\ld\$_"))
})

if (-not $SkipRomLinkSetup -and $missingRomLd.Count -gt 0) {
    Assert-Path 'ROM linker-script setup' $romLinkSetup
    Push-Location $Fmp3Repository
    try {
        Invoke-Checked 'ROM linker-script setup' {
            & $GitBash --login scripts/setup_wifi_ld_links.sh
        }
    }
    finally {
        Pop-Location
    }
}
elseif ($missingRomLd.Count -eq 0) {
    Write-Host 'ROM linker scripts are already available.'
}

$originalPath = $env:PATH
$originalEpoch = $env:SOURCE_DATE_EPOCH
$gitBin = Split-Path -Parent $GitBash

try {
    $env:PATH = '{0};{1};{2}' -f $toolchainBin, $gitBin, $originalPath
    $env:SOURCE_DATE_EPOCH = '1500000000'

    $configureArguments = @(
        '-S', $Fmp3Repository,
        '-B', $BuildDirectory,
        '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
        '-DA1_CHIP=esp32s3',
        "-DA1_VARIANT=$Variant",
        "-DA1_M5_PRC_NUM=$ProcessorCount",
        "-DA1_WIFI_APP=$WifiApplication",
        '-DA1_CONSOLE_USJ=ON',
        "-DA1_ESPTOOL_EXECUTABLE=$esptool",
        "-DA1_M5_EXTERNAL_APPLDIR=$ExternalApplicationDirectory",
        "-DA1_M5_EXTERNAL_APPLNAME=$ExternalApplicationName"
    )
    if (-not [string]::IsNullOrWhiteSpace($ExternalApplicationDirectory)) {
        if ($Variant -ne 'm5') {
            throw 'ExternalApplicationDirectory is supported only for Variant=m5.'
        }
        if ([string]::IsNullOrWhiteSpace($ExternalApplicationName)) {
            throw 'ExternalApplicationDirectoryにはExternalApplicationNameも必要です。'
        }
    }
    $externalObjectPaths = @(
        $ExternalObjects -split '\|' |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
    if ($externalObjectPaths.Count -gt 0) {
        foreach ($externalObject in $externalObjectPaths) {
            Assert-Path 'Arduino external object' $externalObject
        }
    }
    $configureArguments += (
        '-DA1_EXTERNAL_OBJECTS:STRING={0}' -f ($externalObjectPaths -join ';')
    )

    Invoke-Checked 'CMake configure' {
        & $CMake @configureArguments
    }

    Invoke-Checked 'CMake build' {
        & $CMake --build $BuildDirectory --parallel $Parallel
    }
}
finally {
    $env:PATH = $originalPath
    $env:SOURCE_DATE_EPOCH = $originalEpoch
}

$xipDirectory = Join-Path $BuildDirectory 'xip'
$application = Join-Path $xipDirectory 'app_xip.bin'
$elf = Join-Path $xipDirectory 'fmp_xip.elf'
Assert-Path 'application image' $application
Assert-Path 'FMP3 ELF' $elf

$undefined = @(& $nm -u $elf)
if ($LASTEXITCODE -ne 0) {
    throw ('nmによる未定義シンボル検査に失敗しました (exit={0})' -f $LASTEXITCODE)
}
if ($undefined.Count -ne 0) {
    throw ('未定義シンボルが残っています:{0}{1}' -f
        [Environment]::NewLine, ($undefined -join [Environment]::NewLine))
}

Write-Host ''
Write-Host 'Build completed.'
Get-FileHash -Algorithm SHA256 $application, $elf |
    ForEach-Object { Write-Host ('  {0}  {1}' -f $_.Hash, $_.Path) }
Write-Host '  Undefined symbols: 0'
