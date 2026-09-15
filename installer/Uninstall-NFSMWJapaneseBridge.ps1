[CmdletBinding()]
param(
    [string]$GameDir = 'C:\Program Files (x86)\EA GAMES\Need for Speed Most Wanted'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$BackupFolderName = '_NFSMWJapaneseBridge_Backup'
$DefaultWideFixRelativePath = 'scripts\NFSMostWanted.WidescreenFix.ini'
$DefaultCompatLine = 'ImproveGamepadSupport = 0 ; Japanese UI compatibility: optional controller icon TPK is not loaded'

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
$manifestPath = Join-Path $PSScriptRoot 'payload-manifest.json'
$backupRoot = Join-Path $GameDir $BackupFolderName
$statePath = Join-Path $backupRoot 'install-state.json'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'payload-manifest.json is missing.'
}

$manifest = @(Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json)
$state = $null
$stateAvailable = $false
$preExisting = @{}
if (Test-Path -LiteralPath $statePath -PathType Leaf) {
    try {
        $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        $stateAvailable = $true
        if ($state.PSObject.Properties.Name -contains 'PreExisting') {
            foreach ($item in @($state.PreExisting)) { $preExisting[[string]$item] = $true }
        }
    } catch {
        Write-Warning "Install state could not be read; manifest fallback will be used: $statePath"
    }
}

$targetExeRelativePath = 'speed.exe'
if ($stateAvailable -and $state.PSObject.Properties.Name -contains 'TargetExeRelativePath') {
    $targetExeRelativePath = [string]$state.TargetExeRelativePath
}
$speedExe = Get-SafeChildPath $GameDir $targetExeRelativePath
if (-not (Test-Path -LiteralPath $speedExe -PathType Leaf)) {
    throw "Target executable was not found: $speedExe"
}
$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    try { [IO.Path]::GetFullPath($_.Path) -eq $speedExe } catch { $false }
})
if ($running.Count -ne 0) {
    throw 'The target game is running. Exit the game before uninstallation.'
}

$warnings = New-Object 'System.Collections.Generic.List[string]'
$restored = 0
$removed = 0

foreach ($entry in $manifest) {
    $relative = [string]$entry.Path
    $expectedHash = ([string]$entry.Sha256).ToUpperInvariant()
    $target = Get-SafeChildPath $GameDir $relative
    $backup = Get-SafeChildPath $backupRoot $relative
    $targetExists = Test-Path -LiteralPath $target -PathType Leaf
    $backupExists = Test-Path -LiteralPath $backup -PathType Leaf

    if (-not $backupExists -and $preExisting.ContainsKey($relative)) {
        continue
    }

    if ($targetExists -and (Get-Sha256 $target) -ne $expectedHash) {
        $warnings.Add("Preserved modified file: $relative")
        continue
    }

    if ($backupExists) {
        Ensure-ParentDirectory $target
        Copy-Item -LiteralPath $backup -Destination $target -Force
        $restored++
    } elseif ($targetExists -and $stateAvailable) {
        Remove-Item -LiteralPath $target -Force
        $removed++
    } elseif ($targetExists) {
        $warnings.Add("Preserved untracked file because install state is missing: $relative")
    }
}

$wideFixRelativePath = $DefaultWideFixRelativePath
$wideFixOriginalLine = ''
$wideFixInstalledHash = ''
if ($null -ne $state) {
    if ($state.PSObject.Properties.Name -contains 'WideFixRelativePath') {
        $wideFixRelativePath = [string]$state.WideFixRelativePath
    }
    if ($state.PSObject.Properties.Name -contains 'WideFixOriginalLine') {
        $wideFixOriginalLine = [string]$state.WideFixOriginalLine
    }
    if ($state.PSObject.Properties.Name -contains 'WideFixInstalledHash') {
        $wideFixInstalledHash = ([string]$state.WideFixInstalledHash).ToUpperInvariant()
    }
}

$wideFixPath = Get-SafeChildPath $GameDir $wideFixRelativePath
$wideFixBackup = Get-SafeChildPath $backupRoot $wideFixRelativePath
if (Test-Path -LiteralPath $wideFixBackup -PathType Leaf) {
    if ($wideFixOriginalLine.Length -eq 0) {
        $backupText = Get-Content -LiteralPath $wideFixBackup -Raw
        $match = [regex]::Match($backupText, '(?im)^\s*ImproveGamepadSupport\s*=.*$')
        if ($match.Success) { $wideFixOriginalLine = $match.Value }
    }

    if (-not (Test-Path -LiteralPath $wideFixPath -PathType Leaf)) {
        Ensure-ParentDirectory $wideFixPath
        Copy-Item -LiteralPath $wideFixBackup -Destination $wideFixPath
        $restored++
    } else {
        $currentHash = Get-Sha256 $wideFixPath
        if ($wideFixInstalledHash.Length -ne 0 -and $currentHash -eq $wideFixInstalledHash) {
            Copy-Item -LiteralPath $wideFixBackup -Destination $wideFixPath -Force
            $restored++
        } else {
            $currentText = Get-Content -LiteralPath $wideFixPath -Raw
            $currentMatch = [regex]::Match($currentText, '(?im)^\s*ImproveGamepadSupport\s*=.*$')
            if ($currentMatch.Success -and
                ($currentMatch.Value -eq $DefaultCompatLine -or $currentMatch.Value -match '^\s*ImproveGamepadSupport\s*=\s*0\s*(?:;.*)?$')) {
                if ($wideFixOriginalLine.Length -ne 0) {
                    $currentText = [regex]::Replace(
                        $currentText,
                        '(?im)^\s*ImproveGamepadSupport\s*=.*$',
                        $wideFixOriginalLine)
                    Set-Content -LiteralPath $wideFixPath -Value $currentText -Encoding ASCII
                    $restored++
                }
            } else {
                $warnings.Add("Preserved modified WideFix INI: $wideFixRelativePath")
            }
        }
    }
}

foreach ($logRelative in @('NFSMWJapaneseBootstrap.log', 'scripts\NFSMWJapaneseBridge.log')) {
    $logPath = Get-SafeChildPath $GameDir $logRelative
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Remove-Item -LiteralPath $logPath -Force
    }
}

if ($stateAvailable -and $state.PSObject.Properties.Name -contains 'ExecutablePatch' -and
    $null -ne $state.ExecutablePatch) {
    $patch = $state.ExecutablePatch
    $patchTargetRelative = [string]$patch.TargetRelativePath
    $patchTarget = Get-SafeChildPath $GameDir $patchTargetRelative
    $patchBackup = Get-SafeChildPath $backupRoot ([string]$patch.BackupRelativePath)
    $installedHash = ([string]$patch.InstalledSha256).ToUpperInvariant()
    $originalHash = ([string]$patch.OriginalSha256).ToUpperInvariant()
    if (-not (Test-Path -LiteralPath $patchBackup -PathType Leaf) -or
        (Get-Sha256 $patchBackup) -ne $originalHash) {
        $warnings.Add("Executable backup is missing or invalid: $($patch.BackupRelativePath)")
    } elseif (-not (Test-Path -LiteralPath $patchTarget -PathType Leaf)) {
        $warnings.Add("Patched executable is missing: $patchTargetRelative")
    } else {
        $currentExeHash = Get-Sha256 $patchTarget
        if ($currentExeHash -eq $installedHash) {
            Copy-Item -LiteralPath $patchBackup -Destination $patchTarget -Force
            if ((Get-Sha256 $patchTarget) -ne $originalHash) {
                throw "Executable restore verification failed: $patchTargetRelative"
            }
            $restored++
        } elseif ($currentExeHash -ne $originalHash) {
            $warnings.Add("Preserved modified executable: $patchTargetRelative")
        }
    }
}

if ($stateAvailable -and $state.PSObject.Properties.Name -contains 'SpeedExeWasReadOnly') {
    try {
        (Get-Item -LiteralPath $speedExe).IsReadOnly = [bool]$state.SpeedExeWasReadOnly
    } catch {
        $warnings.Add("Could not restore the original $targetExeRelativePath read-only attribute: $($_.Exception.Message)")
    }
}

Write-Host ''
Write-Host 'NFSMW Japanese Bridge was uninstalled.' -ForegroundColor Green
Write-Host "Restored files: $restored"
Write-Host "Removed MOD files: $removed"
Write-Host "Backup retained for safety: $backupRoot"
foreach ($warning in $warnings) { Write-Warning $warning }
if ($warnings.Count -ne 0) { exit 2 }
