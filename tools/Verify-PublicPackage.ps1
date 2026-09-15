[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$PackageRoot = [IO.Path]::GetFullPath($PackageRoot).TrimEnd('\')
$payloadRoot = Join-Path $PackageRoot 'payload'
$manifestPath = Join-Path $PackageRoot 'payload-manifest.json'
$required = @(
    'Install.cmd',
    'Install-NFSMWJapaneseBridge.ps1',
    'Uninstall.cmd',
    'Uninstall-NFSMWJapaneseBridge.ps1',
    'ShaderResourceRestorer.ps1',
    'README.md',
    'payload-manifest.json'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageRoot $relative) -PathType Leaf)) {
        throw "Required release file is missing: $relative"
    }
}

$codePaths = @(
    'version.dll',
    'scripts\NFSMWJapaneseBridge.asi',
    'scripts\NFSMWJapaneseBridge.ini'
)
$manifest = @(Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json)
if ($manifest.Count -ne 43) {
    throw "Expected 43 validated installation entries, found $($manifest.Count)."
}

$actualPayload = @(Get-ChildItem -LiteralPath $payloadRoot -Recurse -File)
if ($actualPayload.Count -ne $codePaths.Count) {
    throw "Public payload must contain only three Bridge files; found $($actualPayload.Count)."
}

foreach ($relative in $codePaths) {
    $entry = @($manifest | Where-Object Path -eq $relative)
    if ($entry.Count -ne 1) { throw "Manifest code entry is missing or duplicated: $relative" }
    $path = Join-Path $payloadRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Code payload is missing: $relative" }
    if ((Get-Item -LiteralPath $path).Length -ne [int64]$entry[0].Length -or
        (Get-Sha256 $path) -ne ([string]$entry[0].Sha256).ToUpperInvariant()) {
        throw "Code payload does not match its manifest: $relative"
    }
}

$forbiddenExtensions = @('.exe', '.vp6', '.bun', '.lzc', '.big', '.idx', '.evt')
$forbiddenNames = @('nfsMW.exe', 'speed.exe', 'dinput8.dll', 'NFSMostWanted.WidescreenFix.asi')
foreach ($file in @(Get-ChildItem -LiteralPath $PackageRoot -Recurse -File)) {
    if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant() -or
        $forbiddenNames -contains $file.Name) {
        throw "Game or third-party binary must not be published: $($file.FullName)"
    }
}

$resourceEntries = @($manifest | Where-Object { $codePaths -notcontains [string]$_.Path })
if ($resourceEntries.Count -ne 40) {
    throw "Expected metadata for 40 user-owned Japanese resources, found $($resourceEntries.Count)."
}
foreach ($entry in $resourceEntries) {
    if (Test-Path -LiteralPath (Join-Path $payloadRoot ([string]$entry.Path)) -PathType Leaf) {
        throw "Japanese game resource was included in the public payload: $($entry.Path)"
    }
}

Write-Host 'PASS public package: 3 Bridge files, 40 resource metadata entries, no game assets.' -ForegroundColor Green
