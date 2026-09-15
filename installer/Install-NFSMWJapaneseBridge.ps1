[CmdletBinding()]
param(
    [string]$GameDir = 'C:\Program Files (x86)\EA GAMES\Need for Speed Most Wanted',
    [switch]$UseExistingJapaneseResources,
    [switch]$ReplaceModifiedJapaneseResources,
    [string]$TargetExe = '',
    [string]$StockShaderSourceExe = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ($UseExistingJapaneseResources -and $ReplaceModifiedJapaneseResources) {
    throw 'UseExistingJapaneseResources and ReplaceModifiedJapaneseResources cannot be used together.'
}

$Version = '1.2.2'
$SupportedExeTargets = @(
    [pscustomobject]@{
        Id = 'NFSPATCHER_EN_13_4GB'
        Length = [int64]6029312
        Sha256 = 'B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06'
        ProtectReadOnly = $false
        RestoreStockShaders = $false
    },
    [pscustomobject]@{
        Id = 'NFSPATCHER_EN_13'
        Length = [int64]6029312
        Sha256 = '80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C'
        ProtectReadOnly = $false
        RestoreStockShaders = $false
    }
)
$StockRcDataFingerprint = 'EED23F70A254C1D2FE32300219D6EE5BE417B72899520BACE959F48FC80B2572'
$PatchedMwArsenalHash = '05873CF968E0BDD021C1E67FF22E9350D22E7F433F1D749323FA6AE27F504700'
$KnownMwArsenalAsiHash = '392DDA59241F0478EC01BEB3FDE53D7F31DFA8AA153A4F7F9BCE9B50A87FFE48'
$KnownMwArsenalResourceHashes = @{
    'GLOBAL\GlobalMemoryFile.bin' = 'DA2DF598DCA42B3090DF0D2ABFC35B97DEDF9030262C1A272D1100C9EEE60E5F'
    'LANGUAGES\Japanese.bin' = '6B7EBB5DD95A9BBD858627B4B74C6A8F84BF5C5BF0F4757316D375A0204C3382'
}
$BackupFolderName = '_NFSMWJapaneseBridge_Backup'
$WideFixRelativePath = 'scripts\NFSMostWanted.WidescreenFix.ini'
$WideFixCompatLine = 'ImproveGamepadSupport = 0 ; Japanese UI compatibility: optional controller icon TPK is not loaded'
$CodeFiles = @(
    'version.dll',
    'scripts\NFSMWJapaneseBridge.asi',
    'scripts\NFSMWJapaneseBridge.ini'
)
$codeFileSet = @{}
foreach ($codeFile in $CodeFiles) { $codeFileSet[$codeFile] = $true }

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-SafeChildPath([string]$Root, [string]$RelativePath) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $candidate = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $candidate.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe relative path in manifest: $RelativePath"
    }
    return $candidate
}

function Ensure-ParentDirectory([string]$Path) {
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

$GameDir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
$speedExe = Join-Path $GameDir 'speed.exe'
$payloadRoot = Join-Path $PSScriptRoot 'payload'
$manifestPath = Join-Path $PSScriptRoot 'payload-manifest.json'
$shaderRestorerPath = Join-Path $PSScriptRoot 'ShaderResourceRestorer.ps1'

if (-not (Test-Path -LiteralPath $shaderRestorerPath -PathType Leaf)) {
    throw 'ShaderResourceRestorer.ps1 is missing. Nothing was installed.'
}
. $shaderRestorerPath

$targetCandidatePaths = New-Object 'System.Collections.Generic.List[string]'
if ($TargetExe.Length -ne 0) {
    $requestedTarget = if ([IO.Path]::IsPathRooted($TargetExe)) {
        [IO.Path]::GetFullPath($TargetExe)
    } else {
        [IO.Path]::GetFullPath((Join-Path $GameDir $TargetExe))
    }
    if ([IO.Path]::GetDirectoryName($requestedTarget).TrimEnd('\') -ne $GameDir) {
        throw 'TargetExe must be an executable directly inside GameDir.'
    }
    $targetCandidatePaths.Add($requestedTarget)
} else {
    $targetCandidatePaths.Add((Join-Path $GameDir 'speed.exe'))
    $targetCandidatePaths.Add((Join-Path $GameDir 'speed_EN-US.exe'))
}

$speedExe = ''
$speedExeTarget = $null
$speedExeHash = ''
$speedExeLength = [int64]0
foreach ($candidatePath in $targetCandidatePaths) {
    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) { continue }
    $candidateLength = (Get-Item -LiteralPath $candidatePath).Length
    $candidateHash = Get-Sha256 $candidatePath
    $matches = @($SupportedExeTargets | Where-Object {
        $_.Length -eq $candidateLength -and $_.Sha256 -eq $candidateHash
    })
    if ($matches.Count -eq 1) {
        $speedExe = $candidatePath
        $speedExeTarget = $matches[0]
        $speedExeHash = $candidateHash
        $speedExeLength = $candidateLength
        break
    }
}
if ($speedExe.Length -eq 0 -or $null -eq $speedExeTarget) {
    throw 'No supported target executable was found. Checked speed.exe before speed_EN-US.exe; nothing was installed.'
}
$speedExeRelativePath = [IO.Path]::GetFileName($speedExe)
$speedExeWasReadOnly = (Get-Item -LiteralPath $speedExe).IsReadOnly
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'dinput8.dll') -PathType Leaf)) {
    throw 'Ultimate ASI Loader (dinput8.dll) was not found. Nothing was installed.'
}
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'scripts\NFSMostWanted.WidescreenFix.asi') -PathType Leaf)) {
    throw 'NFSMostWanted.WidescreenFix.asi was not found. This release expects the clean WideFix environment.'
}
if (-not (Test-Path -LiteralPath (Join-Path $GameDir $WideFixRelativePath) -PathType Leaf)) {
    throw 'NFSMostWanted.WidescreenFix.ini was not found. Nothing was installed.'
}
if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'The release payload is incomplete.'
}

$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    try { [IO.Path]::GetFullPath($_.Path) -eq $speedExe } catch { $false }
})
if ($running.Count -ne 0) {
    throw 'The target game is running. Exit the game before installation.'
}

$manifest = @(Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json)
if ($manifest.Count -lt 1) {
    throw 'The payload manifest is empty.'
}

$compatibilityProfile = 'None'
$compatibleExistingHashes = @{}
$mwArsenalPath = Join-Path $GameDir 'scripts\MWArsenal.asi'
if (Test-Path -LiteralPath $mwArsenalPath -PathType Leaf) {
    $mwArsenalHash = Get-Sha256 $mwArsenalPath
    $knownResourcesMatch = $true
    foreach ($relative in $KnownMwArsenalResourceHashes.Keys) {
        $resourcePath = Get-SafeChildPath $GameDir $relative
        if (-not (Test-Path -LiteralPath $resourcePath -PathType Leaf) -or
            (Get-Sha256 $resourcePath) -ne $KnownMwArsenalResourceHashes[$relative]) {
            $knownResourcesMatch = $false
        }
    }
    if ($mwArsenalHash -eq $KnownMwArsenalAsiHash -and $knownResourcesMatch) {
        $compatibilityProfile = 'MWArsenal_v1.0.3_JapaneseOverlay'
        foreach ($relative in $KnownMwArsenalResourceHashes.Keys) {
            $compatibleExistingHashes[$relative] = $KnownMwArsenalResourceHashes[$relative]
        }
    } elseif ($mwArsenalHash -eq $KnownMwArsenalAsiHash) {
        $compatibilityProfile = 'MWArsenal_v1.0.3'
    } else {
        $compatibilityProfile = 'MWArsenal_unverified'
    }
}

foreach ($entry in $manifest) {
    $relative = [string]$entry.Path
    $source = Get-SafeChildPath $payloadRoot $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        if ($UseExistingJapaneseResources -and -not $codeFileSet.ContainsKey($relative)) {
            continue
        }
        throw "Payload file is missing: $relative"
    }
    if ((Get-Item -LiteralPath $source).Length -ne [int64]$entry.Length -or
        (Get-Sha256 $source) -ne ([string]$entry.Sha256).ToUpperInvariant()) {
        throw "Payload verification failed: $relative"
    }
}

$shaderSourcePath = ''
$shaderSourceHash = ''
$shaderSourceFingerprint = ''
if ([bool]$speedExeTarget.RestoreStockShaders) {
    $shaderCandidates = New-Object 'System.Collections.Generic.List[string]'
    if ($StockShaderSourceExe.Length -ne 0) {
        $shaderCandidates.Add($(if ([IO.Path]::IsPathRooted($StockShaderSourceExe)) {
            [IO.Path]::GetFullPath($StockShaderSourceExe)
        } else {
            [IO.Path]::GetFullPath((Join-Path $GameDir $StockShaderSourceExe))
        }))
    } else {
        $shaderCandidates.Add((Join-Path $GameDir 'speed.exe'))
        $shaderCandidates.Add((Join-Path $GameDir 'nfsMW.exe'))
    }
    foreach ($candidate in $shaderCandidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf) -or
            [IO.Path]::GetFullPath($candidate) -eq [IO.Path]::GetFullPath($speedExe)) {
            continue
        }
        try {
            $fingerprint = Get-NfsmwRcDataFingerprint $candidate
            if ($fingerprint -eq $StockRcDataFingerprint) {
                $shaderSourcePath = [IO.Path]::GetFullPath($candidate)
                $shaderSourceHash = Get-Sha256 $candidate
                $shaderSourceFingerprint = $fingerprint
                break
            }
        } catch {
            Write-Verbose "Rejected shader source $candidate : $($_.Exception.Message)"
        }
    }
    if ($shaderSourcePath.Length -eq 0) {
        throw 'The target needs stock embedded shaders, but no validated stock speed.exe/nfsMW.exe was found. Use -StockShaderSourceExe to select the original executable from your legitimate installation.'
    }
}

$backupRoot = Join-Path $GameDir $BackupFolderName
$statePath = Join-Path $backupRoot 'install-state.json'
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

$oldCreated = @{}
$oldBackedUp = @{}
$oldPreExisting = @{}
$oldPayloadHashes = @{}
$oldExecutablePatch = $null
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    try {
        $oldState = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($item in @($oldState.Created)) { $oldCreated[[string]$item] = $true }
        foreach ($item in @($oldState.BackedUp)) { $oldBackedUp[[string]$item] = $true }
        if ($oldState.PSObject.Properties.Name -contains 'PreExisting') {
            foreach ($item in @($oldState.PreExisting)) { $oldPreExisting[[string]$item] = $true }
        }
        if ($oldState.PSObject.Properties.Name -contains 'SpeedExeWasReadOnly') {
            $speedExeWasReadOnly = [bool]$oldState.SpeedExeWasReadOnly
        }
        if ($oldState.PSObject.Properties.Name -contains 'Payload') {
            foreach ($item in @($oldState.Payload)) {
                $oldPayloadHashes[[string]$item.Path] = ([string]$item.Sha256).ToUpperInvariant()
            }
        }
        if ($oldState.PSObject.Properties.Name -contains 'ExecutablePatch') {
            $oldExecutablePatch = $oldState.ExecutablePatch
        }
    } catch {
        throw "Existing install state is unreadable: $statePath"
    }
}

$executablePatch = $null
if ($null -ne $oldExecutablePatch -and
    [string]$oldExecutablePatch.TargetRelativePath -eq $speedExeRelativePath -and
    $speedExeHash -eq ([string]$oldExecutablePatch.InstalledSha256).ToUpperInvariant()) {
    $executablePatch = $oldExecutablePatch
}

if ([bool]$speedExeTarget.RestoreStockShaders) {
    $exeBackupRelativePath = "executables\$speedExeRelativePath.pre-stock-shaders"
    $exeBackupPath = Get-SafeChildPath $backupRoot $exeBackupRelativePath
    if (Test-Path -LiteralPath $exeBackupPath -PathType Leaf) {
        if ((Get-Item -LiteralPath $exeBackupPath).Length -ne $speedExeLength -or
            (Get-Sha256 $exeBackupPath) -ne $speedExeHash) {
            throw "Existing executable backup does not match the current original target: $exeBackupPath"
        }
    } else {
        Ensure-ParentDirectory $exeBackupPath
        Copy-Item -LiteralPath $speedExe -Destination $exeBackupPath
    }

    $patchTempPath = "$speedExe.nfsmwjb.tmp"
    if (Test-Path -LiteralPath $patchTempPath) {
        throw "Stale executable patch staging file exists: $patchTempPath"
    }
    try {
        Restore-NfsmwStockShaderResources $shaderSourcePath $speedExe $patchTempPath
        $patchedHash = Get-Sha256 $patchTempPath
        if ($patchedHash -ne $PatchedMwArsenalHash) {
            throw "Patched executable hash is not the validated result: $patchedHash"
        }
        Copy-Item -LiteralPath $patchTempPath -Destination $speedExe -Force
    } finally {
        if (Test-Path -LiteralPath $patchTempPath -PathType Leaf) {
            Remove-Item -LiteralPath $patchTempPath -Force
        }
    }
    if ((Get-Sha256 $speedExe) -ne $PatchedMwArsenalHash) {
        throw 'Executable patch verification failed after installation. The original remains in the backup folder.'
    }
    $executablePatch = [pscustomobject][ordered]@{
        TargetRelativePath = $speedExeRelativePath
        OriginalLength = $speedExeLength
        OriginalSha256 = $speedExeHash
        InstalledLength = (Get-Item -LiteralPath $speedExe).Length
        InstalledSha256 = $PatchedMwArsenalHash
        BackupRelativePath = $exeBackupRelativePath
        StockSourceSha256 = $shaderSourceHash
        StockRcDataFingerprint = $shaderSourceFingerprint
        CodeSectionPreserved = $true
    }
}

$created = New-Object 'System.Collections.Generic.List[string]'
$backedUp = New-Object 'System.Collections.Generic.List[string]'
$preExisting = New-Object 'System.Collections.Generic.List[string]'
$preservedModified = New-Object 'System.Collections.Generic.List[string]'

foreach ($entry in $manifest) {
    $relative = [string]$entry.Path
    $source = Get-SafeChildPath $payloadRoot $relative
    $target = Get-SafeChildPath $GameDir $relative
    $backup = Get-SafeChildPath $backupRoot $relative
    $expectedHash = ([string]$entry.Sha256).ToUpperInvariant()
    $targetExists = Test-Path -LiteralPath $target -PathType Leaf
    $targetHash = if ($targetExists) { Get-Sha256 $target } else { '' }
    $changedSincePreviousRelease = $targetExists -and $oldPayloadHashes.ContainsKey($relative) -and
        $targetHash -ne $oldPayloadHashes[$relative] -and $targetHash -ne $expectedHash
    $knownCompatibleExisting = $targetExists -and $compatibleExistingHashes.ContainsKey($relative) -and
        $targetHash -eq $compatibleExistingHashes[$relative]

    if (-not $ReplaceModifiedJapaneseResources -and
        -not $codeFileSet.ContainsKey($relative) -and
        ($changedSincePreviousRelease -or $knownCompatibleExisting)) {
        if ($oldBackedUp.ContainsKey($relative) -and -not (Test-Path -LiteralPath $backup -PathType Leaf)) {
            throw "Backup for a modified resource is missing: $relative"
        }
        if (Test-Path -LiteralPath $backup -PathType Leaf) {
            $backedUp.Add($relative)
        } elseif ($oldCreated.ContainsKey($relative)) {
            $created.Add($relative)
        } else {
            $preExisting.Add($relative)
        }
        $preservedModified.Add($relative)
        continue
    }
    if ($codeFileSet.ContainsKey($relative) -and $changedSincePreviousRelease) {
        throw "Installed Japanese Bridge code was modified; update stopped without overwriting it: $relative"
    }

    if ($UseExistingJapaneseResources -and -not $codeFileSet.ContainsKey($relative)) {
        if (-not $targetExists -or
            (Get-Item -LiteralPath $target).Length -ne [int64]$entry.Length -or
            $targetHash -ne $expectedHash) {
            throw "Existing Japanese resource does not match the validated release: $relative"
        }
        $preExisting.Add($relative)
        continue
    }

    if (Test-Path -LiteralPath $backup -PathType Leaf) {
        $backedUp.Add($relative)
    } elseif ($targetExists) {
        if ($oldCreated.ContainsKey($relative)) {
            $created.Add($relative)
        } elseif ($oldPreExisting.ContainsKey($relative) -or
                  $targetHash -eq $expectedHash) {
            $preExisting.Add($relative)
        } else {
            Ensure-ParentDirectory $backup
            Copy-Item -LiteralPath $target -Destination $backup
            $backedUp.Add($relative)
        }
    } else {
        $created.Add($relative)
    }

    Ensure-ParentDirectory $target
    Copy-Item -LiteralPath $source -Destination $target -Force
}

$wideFixPath = Get-SafeChildPath $GameDir $WideFixRelativePath
$wideFixBackup = Get-SafeChildPath $backupRoot $WideFixRelativePath
if (-not (Test-Path -LiteralPath $wideFixBackup -PathType Leaf)) {
    Ensure-ParentDirectory $wideFixBackup
    Copy-Item -LiteralPath $wideFixPath -Destination $wideFixBackup
}

$wideFixOriginalText = Get-Content -LiteralPath $wideFixBackup -Raw
$originalMatch = [regex]::Match($wideFixOriginalText, '(?im)^\s*ImproveGamepadSupport\s*=.*$')
$wideFixOriginalLine = if ($originalMatch.Success) { $originalMatch.Value } else { '' }
$wideFixText = Get-Content -LiteralPath $wideFixPath -Raw
if ([regex]::IsMatch($wideFixText, '(?im)^\s*ImproveGamepadSupport\s*=.*$')) {
    $wideFixText = [regex]::Replace(
        $wideFixText,
        '(?im)^\s*ImproveGamepadSupport\s*=.*$',
        $WideFixCompatLine)
} else {
    $wideFixText = $wideFixText.TrimEnd("`r", "`n") + "`r`n$WideFixCompatLine`r`n"
}
Set-Content -LiteralPath $wideFixPath -Value $wideFixText -Encoding ASCII
$wideFixInstalledHash = Get-Sha256 $wideFixPath
$speedExeInstalledReadOnly = [bool]($speedExeWasReadOnly -or [bool]$speedExeTarget.ProtectReadOnly)

$state = [ordered]@{
    Product = 'NFSMW Japanese Bridge'
    Version = $Version
    InstalledAt = (Get-Date).ToString('o')
    GameDir = $GameDir
    TargetExeRelativePath = $speedExeRelativePath
    SpeedExeSha256 = $speedExeHash
    SpeedExeTargetId = [string]$speedExeTarget.Id
    SpeedExeWasReadOnly = $speedExeWasReadOnly
    SpeedExeReadOnlyRequired = [bool]$speedExeTarget.ProtectReadOnly
    SpeedExeReadOnlyInstalled = $speedExeInstalledReadOnly
    ExecutablePatch = $executablePatch
    Created = @($created)
    BackedUp = @($backedUp)
    PreExisting = @($preExisting)
    PreservedModified = @($preservedModified)
    CompatibilityProfile = $compatibilityProfile
    InstallMode = if ($UseExistingJapaneseResources) {
        'ExistingJapaneseResources'
    } elseif ($ReplaceModifiedJapaneseResources) {
        'FullPayloadReplaceValidatedResources'
    } else {
        'FullPayload'
    }
    Payload = @($manifest)
    WideFixRelativePath = $WideFixRelativePath
    WideFixOriginalLine = $wideFixOriginalLine
    WideFixInstalledHash = $wideFixInstalledHash
}
$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statePath -Encoding UTF8
try {
    (Get-Item -LiteralPath $speedExe).IsReadOnly = $speedExeInstalledReadOnly
} catch {
    throw "Installation files were written, but $speedExeRelativePath could not retain its required read-only state. Run the uninstaller before retrying: $($_.Exception.Message)"
}
if ((Get-Item -LiteralPath $speedExe).IsReadOnly -ne $speedExeInstalledReadOnly) {
    throw 'Installation files were written, but speed.exe did not retain the required attribute state. Run the uninstaller before retrying.'
}

Write-Host ''
Write-Host "NFSMW Japanese Bridge v$Version installed successfully." -ForegroundColor Green
Write-Host "Game directory: $GameDir"
Write-Host "Backup: $backupRoot"
Write-Host "Executable target: $speedExeRelativePath ($($speedExeTarget.Id))"
if ($null -ne $executablePatch) {
    Write-Host "Embedded shaders: validated stock set ($($executablePatch.StockRcDataFingerprint))"
}
Write-Host "Compatibility profile: $compatibilityProfile"
if ($speedExeTarget.ProtectReadOnly) {
    Write-Host "$speedExeRelativePath was protected as read-only; its previous attribute is recorded for uninstallation."
} else {
    Write-Host "$speedExeRelativePath does not require read-only protection; its previous attribute was preserved."
}
if ($preservedModified.Count -ne 0) {
    Write-Host "Preserved modified resources: $($preservedModified.Count)"
}
if ($ReplaceModifiedJapaneseResources) {
    Write-Host 'Validated Japanese payload was explicitly selected over modified/alternate resources.'
}
Write-Host 'Registry values and save files were not changed.'
