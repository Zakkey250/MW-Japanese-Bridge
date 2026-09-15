[CmdletBinding()]
param(
    [string]$Version = '1.2.2',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Copy-Required([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing build input: $Source" }
    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if ($OutputRoot.Length -eq 0) { $OutputRoot = Join-Path $root 'dist' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
$name = "MW-Japanese-Bridge-v$Version"
$stage = [IO.Path]::GetFullPath((Join-Path $OutputRoot $name))
$zip = [IO.Path]::GetFullPath((Join-Path $OutputRoot "$name.zip"))
if ([IO.Path]::GetDirectoryName($stage).TrimEnd('\') -ne $OutputRoot) { throw "Unsafe output path: $stage" }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'vswhere.exe was not found.' }
$msbuild = @(& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild.exe was not found.' }

foreach ($project in @('NFSMWJapaneseBridge.vcxproj', 'NFSMWJapaneseBootstrap.vcxproj', 'NFSMWJapaneseBridge.Tests.vcxproj')) {
    & $msbuild (Join-Path $root $project) /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m:1 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed: $project" }
}
$testExe = Join-Path $root 'artifacts\Release\NFSMWJapaneseBridge.Tests.exe'
& $testExe
if ($LASTEXITCODE -ne 0) { throw "Native unit tests failed: exit=$LASTEXITCODE" }

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
New-Item -ItemType Directory -Path (Join-Path $stage 'payload\scripts') -Force | Out-Null

Copy-Required (Join-Path $root 'artifacts\Release\version.dll') (Join-Path $stage 'payload\version.dll')
Copy-Required (Join-Path $root 'artifacts\Release\NFSMWJapaneseBridge.asi') (Join-Path $stage 'payload\scripts\NFSMWJapaneseBridge.asi')
Copy-Required (Join-Path $root 'NFSMWJapaneseBridge.ini') (Join-Path $stage 'payload\scripts\NFSMWJapaneseBridge.ini')
Copy-Required (Join-Path $root 'README.md') (Join-Path $stage 'README.md')
Copy-Required (Join-Path $root 'THIRD_PARTY_NOTICES.md') (Join-Path $stage 'THIRD_PARTY_NOTICES.md')
Copy-Required (Join-Path $root 'installer\Install-JPBase.cmd') (Join-Path $stage 'Install.cmd')
Copy-Required (Join-Path $root 'installer\Uninstall.cmd') (Join-Path $stage 'Uninstall.cmd')
foreach ($file in @('Install-NFSMWJapaneseBridge.ps1', 'Uninstall-NFSMWJapaneseBridge.ps1', 'ShaderResourceRestorer.ps1')) {
    Copy-Required (Join-Path $root "installer\$file") (Join-Path $stage $file)
}

$manifestSource = Join-Path $root 'manifests\validated-payload-manifest.json'
$manifest = @(Get-Content -LiteralPath $manifestSource -Raw -Encoding UTF8 | ConvertFrom-Json)
foreach ($relative in @('version.dll', 'scripts\NFSMWJapaneseBridge.asi', 'scripts\NFSMWJapaneseBridge.ini')) {
    $entry = @($manifest | Where-Object Path -eq $relative)
    if ($entry.Count -ne 1) { throw "Manifest code entry is missing or duplicated: $relative" }
    $path = Join-Path (Join-Path $stage 'payload') $relative
    $entry[0].Length = (Get-Item -LiteralPath $path).Length
    $entry[0].Sha256 = Get-Sha256 $path
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'payload-manifest.json') -Encoding UTF8

$metadata = [ordered]@{
    Product = 'MW Japanese Bridge'
    Version = $Version
    BuiltAt = (Get-Date).ToString('o')
    PublicPayloadFiles = 3
    ValidatedUserOwnedResources = 40
    IncludesGameAssets = $false
    SupportedExecutables = @(
        [ordered]@{ Id='NFSPATCHER_EN_13'; Length=6029312; Sha256='80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C'; LargeAddressAware=$false },
        [ordered]@{ Id='NFSPATCHER_EN_13_4GB'; Length=6029312; Sha256='B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06'; LargeAddressAware=$true }
    )
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'release-metadata.json') -Encoding UTF8
Copy-Required (Join-Path $root 'tools\Verify-PublicPackage.ps1') (Join-Path $stage 'Verify-Package.ps1')

$sums = Join-Path $stage 'SHA256SUMS.txt'
@(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object FullName -ne $sums | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
    "$(Get-Sha256 $_.FullName)  $relative"
}) | Set-Content -LiteralPath $sums -Encoding ASCII

& (Join-Path $stage 'Verify-Package.ps1') -PackageRoot $stage
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
[ordered]@{ Stage=$stage; Zip=$zip; ZipLength=(Get-Item -LiteralPath $zip).Length; ZipSha256=Get-Sha256 $zip } | ConvertTo-Json
