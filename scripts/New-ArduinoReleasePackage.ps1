<#
.SYNOPSIS
    Creates the deterministic Arduino library ZIP used as a GitHub Release asset.
#>

[CmdletBinding()]
param(
    [string]$M5ArduinoRoot = '',
    [string]$Allowlist = '',
    [string]$OutputDirectory = '',
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($M5ArduinoRoot)) {
    $M5ArduinoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if ([string]::IsNullOrWhiteSpace($Allowlist)) {
    $Allowlist = Join-Path $M5ArduinoRoot 'packaging\release-allowlist.json'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $M5ArduinoRoot 'build\release'
}

$root = [System.IO.Path]::GetFullPath($M5ArduinoRoot)
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$defaultReleaseRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $root 'build\release'))
$releasePrefix = $defaultReleaseRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar
) + [System.IO.Path]::DirectorySeparatorChar
if (($output -ne $defaultReleaseRoot) -and (-not $output.StartsWith(
        $releasePrefix, [System.StringComparison]::OrdinalIgnoreCase))) {
    throw "OutputDirectory must be inside $defaultReleaseRoot"
}

$definition = Get-Content -LiteralPath $Allowlist -Raw -Encoding utf8 |
    ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($definition.packageName)) {
    throw 'The release allowlist does not define packageName.'
}

$propertiesPath = Join-Path $root 'library.properties'
$versionLine = Get-Content -LiteralPath $propertiesPath -Encoding utf8 |
    Where-Object { $_ -match '^version=' } |
    Select-Object -First 1
if ($null -eq $versionLine) {
    throw 'library.properties does not contain version=.'
}
$version = $versionLine.Substring('version='.Length).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+([+-][0-9A-Za-z.-]+)?$') {
    throw "Unsupported library version: $version"
}

$packageName = [string]$definition.packageName
$stagingDirectory = Join-Path $output 'staging'
$packageDirectory = Join-Path $stagingDirectory $packageName
$zipName = "$packageName-$version.zip"
$zipPath = Join-Path $output $zipName
$hashPath = "$zipPath.sha256"

foreach ($target in @($packageDirectory, $zipPath, $hashPath)) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}
[void](New-Item -ItemType Directory -Path $packageDirectory -Force)

foreach ($entry in $definition.entries) {
    $source = [System.IO.Path]::GetFullPath(
        (Join-Path $root ([string]$entry.source)))
    $rootPrefix = $root.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    if (($source -ne $root) -and (-not $source.StartsWith(
            $rootPrefix, [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Allowlist source escapes the repository: $source"
    }
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Allowlist source was not found: $source"
    }
    $destination = Join-Path $packageDirectory ([string]$entry.destination)
    $destinationParent = Split-Path -Parent $destination
    [void](New-Item -ItemType Directory -Path $destinationParent -Force)
    if ((Get-Item -LiteralPath $source).PSIsContainer) {
        Copy-Item -LiteralPath $source -Destination $destination -Recurse
    }
    else {
        Copy-Item -LiteralPath $source -Destination $destination
    }
}

$sourceCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not read the source Git commit.'
}
$sourceStatus = @(& git -C $root status --short)
if (($sourceStatus.Count -ne 0) -and (-not $AllowDirty)) {
    throw 'The source worktree is dirty. Commit it or use -AllowDirty for a non-release test.'
}
$manifestDirectory = Join-Path $packageDirectory 'extras'
[void](New-Item -ItemType Directory -Path $manifestDirectory -Force)
$manifest = [ordered]@{
    package = $packageName
    version = $version
    sourceCommit = $sourceCommit
    sourceDirty = ($sourceStatus.Count -ne 0)
    packageProfile = [string]$definition.packageProfile
    m5stackArduinoCore = [string]$definition.m5stackArduinoCore
    fmp3RuntimeIncluded = [bool]$definition.fmp3RuntimeIncluded
    dualCoreIncluded = [bool]$definition.dualCoreIncluded
    m5UnifiedIncluded = [bool]$definition.m5UnifiedIncluded
    wifiIncluded = [bool]$definition.wifiIncluded
    fmp3CoreCommit = [string]$definition.fmp3CoreCommit
    esp32s3PortBaseCommit = [string]$definition.esp32s3PortBaseCommit
    esp32s3PortPatches = @($definition.esp32s3PortPatches)
    allowlist = @($definition.entries | ForEach-Object {
        [ordered]@{
            source = [string]$_.source
            destination = [string]$_.destination
        }
    })
}
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $manifestDirectory 'manifest.json') `
        -Encoding utf8

#
#  Developer-machine paths must not reach the package.
#
#  This was a blocklist of specific known paths, one set per developer.
#  Two things were wrong with that.
#
#  It could only catch the developer it was written for. And it missed a live
#  leak: it built each root with backslashes and with JSON-escaped backslashes,
#  but never with FORWARD slashes, which is what CMake writes on Windows - so
#  the packager's own directory reached 15 of the 47 shipped objects and every
#  final image, and this check passed. (An earlier fix here addressed a related
#  doubled-backslash bug; it did not occur to me then that the separator could
#  differ at all.)
#
#  The roots below are still checked, because the current machine's paths are
#  the ones most likely to leak, and now in every separator form. The general
#  check - anything SHAPED like a build-machine path, whoever built it - lives
#  in scripts/check_host_paths.py and runs on the assembled release.
#
$forbiddenRoots = @()
foreach ($candidate in @($env:USERPROFILE, $root)) {
    if (-not [string]::IsNullOrWhiteSpace($candidate)) {
        $forbiddenRoots += $candidate.TrimEnd('\', '/')
    }
}
$forbiddenPatterns = @()
foreach ($forbiddenRoot in ($forbiddenRoots | Sort-Object -Unique)) {
    $forbiddenPatterns += $forbiddenRoot
    #  The same path as it appears inside JSON.
    $forbiddenPatterns += $forbiddenRoot.Replace('\', '\\')
    #  And as CMake, ninja and the compiler write it on Windows. Leaving this
    #  form out is what let the leak through.
    $forbiddenPatterns += $forbiddenRoot.Replace('\', '/')
}
foreach ($file in Get-ChildItem -LiteralPath $packageDirectory -Recurse -File) {
    if ($file.Extension -in @('.a', '.bin', '.elf', '.png', '.jpg', '.pdf')) {
        continue
    }
    $content = Get-Content -LiteralPath $file.FullName -Raw -ErrorAction SilentlyContinue
    foreach ($pattern in $forbiddenPatterns) {
        if ($content -match [regex]::Escape($pattern)) {
            throw "Package contains a developer-machine path in $($file.FullName): $pattern"
        }
    }
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[void](New-Item -ItemType Directory -Path $output -Force)
$archive = [System.IO.Compression.ZipFile]::Open(
    $zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $fixedTimestamp = [DateTimeOffset]::new(
        2026, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
    $files = Get-ChildItem -LiteralPath $packageDirectory -Recurse -File |
        Sort-Object FullName
    foreach ($file in $files) {
        $insidePackage = $file.FullName.Substring(
            $packageDirectory.Length + 1).Replace('\', '/')
        $entryName = "$packageName/$insidePackage"
        $zipEntry = $archive.CreateEntry(
            $entryName, [System.IO.Compression.CompressionLevel]::Optimal)
        $zipEntry.LastWriteTime = $fixedTimestamp
        $input = [System.IO.File]::OpenRead($file.FullName)
        $entryStream = $zipEntry.Open()
        try {
            $input.CopyTo($entryStream)
        }
        finally {
            $entryStream.Dispose()
            $input.Dispose()
        }
    }
}
finally {
    $archive.Dispose()
}

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $zipName" | Set-Content -LiteralPath $hashPath -Encoding ascii

Write-Host ''
Write-Host 'Arduino Release asset package created.'
Write-Host "  ZIP:    $zipPath"
Write-Host "  SHA256: $hash"
Write-Host "  Root:   $packageName/"
Write-Host "  Profile: $($definition.packageProfile)"
