[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$DataRoot,
    [Parameter(Mandatory = $true)][string]$StageRoot,
    [string]$PackagePath = '',
    [string]$ManifestPath = '',
    [string]$TagName = '',
    [string]$AssetName = '',
    [string]$ExpectedSha256 = '',
    [int]$RuntimePid = 0,
    [int]$Port = 0,
    [string]$CurrentVersion = '',
    [switch]$ValidateOnly,
    [switch]$RecoverInterrupted,
    # Explicit test-only fault injection used by the isolated Windows CI fixture.
    # The dashboard/API never passes this switch.
    [switch]$TestForceFailureAfterReplace,
    [switch]$TestForceRollbackFailure,
    [switch]$TestCrashAfterReplace
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$logDirectory = Join-Path $DataRoot 'logs'
$logPath = Join-Path $logDirectory 'update-apply.log'
$statusPath = Join-Path $StageRoot 'apply-status.json'
$extractRoot = $null
$newRoot = $null
$backupRoot = $null
$previousVersion = $null
$incomingFiles = @()
$filesModified = $false
$newRuntime = $null
$installedVersion = $TagName -replace '^v', ''

function Write-UpdateLog([string]$Level, [string]$Message) {
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    $line = '{0} {1} {2}' -f [DateTimeOffset]::UtcNow.ToString('o'), $Level, $Message
    Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8
}

function Write-UpdateStatus([string]$State, [string]$Message, [string]$Backup = '') {
    $record = [ordered]@{
        generated_utc = [DateTimeOffset]::UtcNow.ToString('o')
        state = $State
        tag_name = $TagName
        previous_version = $previousVersion
        target_version = $installedVersion
        backup_path = $Backup
        install_root = [System.IO.Path]::GetFullPath($InstallRoot)
        runtime_port = $Port
        helper_pid = $PID
        message = $Message
    }
    $temporary = "$statusPath.part"
    $json = $record | ConvertTo-Json -Depth 4
    # Windows PowerShell 5.1 Set-Content -Encoding UTF8 writes a BOM, which
    # the C++ JSON reader does not accept consistently. Keep the status file UTF-8
    # without a BOM and replace the small status record atomically.
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($temporary, $json, $utf8NoBom)
    Move-Item -LiteralPath $temporary -Destination $statusPath -Force
}

function Get-Sha256([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

function Test-Health([int]$ExpectedPort, [string]$ExpectedVersion) {
    try {
        $health = Invoke-RestMethod -UseBasicParsing -Uri "http://127.0.0.1:$ExpectedPort/health" -TimeoutSec 1
        return ($health.status -eq 'ok' -and $health.version -eq $ExpectedVersion)
    } catch {
        return $false
    }
}

function Test-InstalledRuntimeHealth([int]$ExpectedPort, [string]$ExpectedVersion, [string]$RootFull) {
    if (-not (Test-Health $ExpectedPort $ExpectedVersion)) { return $false }
    $expectedExe = [System.IO.Path]::GetFullPath((Join-Path $RootFull 'local-ai-runtime.exe'))
    try {
        foreach ($listener in @(Get-NetTCPConnection -LocalPort $ExpectedPort -State Listen -ErrorAction Stop)) {
            try {
                $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction Stop
                if ($processInfo -and $processInfo.ExecutablePath -and
                    [System.IO.Path]::GetFullPath([string]$processInfo.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)) {
                    return $true
                }
            } catch { }
        }
    } catch { }
    return $false
}

function Wait-Health([int]$ExpectedPort, [string]$ExpectedVersion, [int]$Seconds) {
    for ($attempt = 0; $attempt -lt ($Seconds * 4); $attempt++) {
        if (Test-Health $ExpectedPort $ExpectedVersion) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Start-Runtime([string]$VersionToStart) {
    $runtime = Join-Path $InstallRoot 'local-ai-runtime.exe'
    $configPath = Join-Path $DataRoot 'config/engines.json'
    $webPath = Join-Path $InstallRoot 'web'
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        throw "User configuration is missing; refusing to start update target: $configPath"
    }
    $logSuffix = "$($VersionToStart -replace '[^A-Za-z0-9.-]', '_')-$([Guid]::NewGuid().ToString('N'))"
    $stdout = Join-Path $logDirectory "runtime-after-update-$logSuffix.stdout.log"
    $stderr = Join-Path $logDirectory "runtime-after-update-$logSuffix.stderr.log"
    $arguments = '--config "{0}" --web-dir "{1}"' -f $configPath, $webPath
    return Start-Process -FilePath $runtime -ArgumentList $arguments -WorkingDirectory $DataRoot `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
}

function Restore-PreviousFiles {
    if ($TestForceRollbackFailure) {
        throw 'Intentional test-only rollback failure to verify recovery status and backup retention.'
    }
    if (-not $incomingFiles -or -not (Test-Path -LiteralPath $backupRoot -PathType Container)) { return }
    foreach ($entry in $incomingFiles) {
        $relative = [string]$entry.path
        $destination = Join-Path $InstallRoot $relative
        if ($entry.existed_before) {
            $backup = Join-Path $backupRoot $relative
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                throw "Rollback data is missing for '$relative'; the backup was not complete."
            }
            if ([string]$entry.backup_sha256 -notmatch '^[0-9a-f]{64}$' -or
                (Get-Sha256 $backup) -cne [string]$entry.backup_sha256) {
                throw "Rollback data failed SHA-256 verification for '$relative'."
            }
            $parent = Split-Path -Parent $destination
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
            Copy-Item -LiteralPath $backup -Destination $destination -Force
            if ((Get-Sha256 $destination) -cne [string]$entry.backup_sha256) {
                throw "Restored file did not match its verified backup: '$relative'."
            }
        } else {
            if (Test-Path -LiteralPath $destination) {
                $destinationItem = Get-Item -LiteralPath $destination -Force
                if ($destinationItem.PSIsContainer -or (($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
                    throw "Refusing to remove a directory or reparse point created at '$destination'."
                }
                Remove-Item -LiteralPath $destination -Force
            }
        }
    }
}

function Write-RecoveryStatus($OriginalStatus, [string]$State, [string]$Message, [string]$Backup) {
    $record = [ordered]@{
        generated_utc = [DateTimeOffset]::UtcNow.ToString('o')
        state = $State
        tag_name = [string]$OriginalStatus.tag_name
        previous_version = [string]$OriginalStatus.previous_version
        target_version = [string]$OriginalStatus.target_version
        backup_path = $Backup
        install_root = [System.IO.Path]::GetFullPath($InstallRoot)
        runtime_port = [int]$OriginalStatus.runtime_port
        helper_pid = $PID
        message = $Message
    }
    $temporary = "$statusPath.part"
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($temporary, ($record | ConvertTo-Json -Depth 4), $utf8NoBom)
    Move-Item -LiteralPath $temporary -Destination $statusPath -Force
}

function Test-UpdateHelperRunning($Status, [string]$StageRootFull) {
    $helperPid = 0
    try { $helperPid = [int]$Status.helper_pid } catch { return $false }
    if ($helperPid -le 0 -or $helperPid -eq $PID) { return $false }

    try {
        $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $helperPid" -ErrorAction Stop
    } catch {
        throw "Could not determine whether update helper PID $helperPid is still running. Recovery is paused to avoid racing an active update."
    }
    if (-not $processInfo) { return $false }
    if ([string]$processInfo.Name -notmatch '^(?i:powershell|pwsh)\.exe$') { return $false }

    $expectedHelper = [System.IO.Path]::GetFullPath((Join-Path $StageRootFull 'Apply-LocalAIRuntimeUpdate.ps1'))
    $commandLine = [string]$processInfo.CommandLine
    return $commandLine.IndexOf($expectedHelper, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function Test-SafeUpdateRelativePath([string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or $RelativePath.StartsWith('/') -or
        $RelativePath.StartsWith('\') -or $RelativePath.Contains(':') -or $RelativePath.Contains('/')) {
        return $false
    }
    $parts = $RelativePath.Split('\')
    if (@($parts | Where-Object { [string]::IsNullOrWhiteSpace($_) -or $_ -eq '.' -or $_ -eq '..' }).Count -gt 0) {
        return $false
    }
    return (($RelativePath -match '^local-ai-runtime\.exe$') -or
        ($RelativePath -match '^[^\\]+\.dll$') -or
        ($RelativePath -in @('LICENSE', 'README-INSTALL.txt', 'Start-LocalAIRuntime.cmd', 'config\engines.example.json',
                              'scripts\Start-LocalAIRuntime.ps1', 'scripts\Apply-LocalAIRuntimeUpdate.ps1')) -or
        $RelativePath.StartsWith('web\', [StringComparison]::OrdinalIgnoreCase))
}

function Test-InstallMatchesRollbackManifest($Entries, [string]$BackupFull, [string]$RootFull) {
    foreach ($entry in $Entries) {
        $destination = Join-Path $RootFull ([string]$entry.path)
        if ($entry.existed_before) {
            $backup = Join-Path $BackupFull ([string]$entry.path)
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or
                (Get-Sha256 $destination) -cne [string]$entry.backup_sha256) {
                return $false
            }
        } elseif (Test-Path -LiteralPath $destination) {
            return $false
        }
    }
    return $true
}

function Stop-InstalledRuntimeForRecovery([string]$RootFull, [int]$ExpectedPort) {
    $expectedExe = [System.IO.Path]::GetFullPath((Join-Path $RootFull 'local-ai-runtime.exe'))
    $processes = @(Get-CimInstance Win32_Process -Filter "Name = 'local-ai-runtime.exe'" -ErrorAction Stop |
        Where-Object {
            $_.ExecutablePath -and
            [System.IO.Path]::GetFullPath([string]$_.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)
        })
    foreach ($processInfo in $processes) {
        $processId = [int]$processInfo.ProcessId
        if ($ExpectedPort -ge 1 -and $ExpectedPort -le 65535) {
            $ownsExpectedPort = $false
            try {
                $ownsExpectedPort = @(
                    Get-NetTCPConnection -LocalPort $ExpectedPort -State Listen -ErrorAction Stop |
                        Where-Object { [int]$_.OwningProcess -eq $processId }
                ).Count -gt 0
            } catch { }
            if ($ownsExpectedPort) {
                try {
                    $null = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$ExpectedPort/v1/runtime/shutdown" `
                        -Method Post -ContentType 'application/json' -Body '{}' -TimeoutSec 3 -ErrorAction Stop
                } catch { }
            }
        }
        for ($attempt = 0; $attempt -lt 40; $attempt++) {
            if (-not (Get-Process -Id $processId -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 250
        }
        if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
            # Re-check the executable path immediately before force-stopping it.
            $current = Get-CimInstance Win32_Process -Filter "ProcessId = $processId" -ErrorAction SilentlyContinue
            if ($current -and $current.ExecutablePath -and
                [System.IO.Path]::GetFullPath([string]$current.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id $processId -Force -ErrorAction Stop
            }
        }
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            if (-not (Get-Process -Id $processId -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 250
        }
        if (Get-Process -Id $processId -ErrorAction SilentlyContinue) {
            throw "Could not stop the Local AI Runtime process ($processId) before recovery. The verified backup was retained."
        }
    }
}

function Invoke-RecoverInterrupted {
    $rootFull = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
    $dataFull = [System.IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
    $stageFull = [System.IO.Path]::GetFullPath($StageRoot).TrimEnd('\')
    if ([System.IO.Path]::GetPathRoot($rootFull).TrimEnd('\') -eq $rootFull) {
        throw 'Refusing recovery against a drive root.'
    }
    if ($rootFull.Equals($dataFull, [StringComparison]::OrdinalIgnoreCase) -or
        $rootFull.StartsWith($dataFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $dataFull.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The application folder and user-data folder must be separate.'
    }
    $updatesFull = [System.IO.Path]::GetFullPath((Join-Path $dataFull 'updates')).TrimEnd('\')
    if (-not $stageFull.StartsWith($updatesFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not [System.IO.Path]::GetDirectoryName($stageFull).Equals($updatesFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The recovery stage must be a direct release-tag child of the user-data updates folder.'
    }
    if ((Split-Path -Leaf $stageFull) -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$') {
        throw 'The recovery stage folder name is not a valid release tag.'
    }

    $rootItem = Get-Item -LiteralPath $rootFull -Force -ErrorAction Stop
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Refusing to recover an application directory that is a symbolic link or junction.'
    }
    $stageItem = Get-Item -LiteralPath $stageFull -Force -ErrorAction Stop
    if (($stageItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Refusing to recover through a symbolic link or junction in the update stage.'
    }
    $status = Get-Content -LiteralPath $statusPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ([string]$status.tag_name -ne (Split-Path -Leaf $stageFull)) {
        throw 'The interrupted-update status does not match the release-tag stage folder.'
    }
    if (-not $status.install_root -or
        -not [System.IO.Path]::GetFullPath([string]$status.install_root).Equals($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'This pending update belongs to a different installation folder. Do not recover it against the current folder.'
    }
    $state = [string]$status.state
    if ($state -notin @('applying', 'stopping', 'replacing', 'awaiting_health', 'rollback_failed')) {
        return
    }
    if (Test-UpdateHelperRunning $status $stageFull) {
        throw "The update helper is still running for $($status.tag_name). Wait for it to finish before starting this installation."
    }

    $backupFull = ''
    try {
        if ($state -eq 'applying' -and [string]::IsNullOrWhiteSpace([string]$status.backup_path)) {
            Write-RecoveryStatus $status 'failed' 'The update helper stopped during package validation, before application files could be changed. The existing installation was left intact.' ''
            Write-UpdateLog 'WARN' "update.interrupted_before_replace tag=$($status.tag_name) state=applying"
            return
        }

        $backupFull = [System.IO.Path]::GetFullPath([string]$status.backup_path).TrimEnd('\')
        if (-not $backupFull.StartsWith($stageFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
            -not [System.IO.Path]::GetDirectoryName($backupFull).Equals($stageFull, [StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $backupFull) -notmatch '^rollback-\d{8}T\d{6}Z-[0-9a-fA-F]{32}$') {
            throw 'The rollback folder path is missing or outside the expected update stage. No files were restored.'
        }
        $backupItem = Get-Item -LiteralPath $backupFull -Force -ErrorAction Stop
        if (-not $backupItem.PSIsContainer -or ($backupItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'The rollback folder is missing or is a symbolic link/junction.'
        }

        # If the helper died before touching application files, leave the healthy
        # installation alone. For an interrupted rollback, mark it rolled back;
        # for an interrupted apply, allow the user to retry.
        $rollbackManifestPath = Join-Path $backupFull 'rollback-manifest.json'
        if (-not (Test-Path -LiteralPath $rollbackManifestPath -PathType Leaf)) {
            throw 'The rollback manifest is missing. Preserve the stage folder for manual recovery.'
        }
        $entries = @(Get-Content -LiteralPath $rollbackManifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop)
        if ($entries.Count -lt 4) { throw 'The rollback manifest is incomplete. Preserve the stage folder for manual recovery.' }
        $seenPaths = @{}
        $requiredPaths = @('local-ai-runtime.exe', 'web\index.html', 'scripts\Start-LocalAIRuntime.ps1', 'scripts\Apply-LocalAIRuntimeUpdate.ps1')
        foreach ($entry in $entries) {
            $relative = [string]$entry.path
            if (-not (Test-SafeUpdateRelativePath $relative)) { throw "Unsafe path in rollback manifest: '$relative'." }
            if ($seenPaths.ContainsKey($relative)) { throw "Duplicate path in rollback manifest: '$relative'." }
            $seenPaths[$relative] = $true
            if ($null -eq $entry.existed_before) { throw "Rollback manifest is missing the pre-update state for '$relative'." }
            if ($entry.existed_before) {
                $backupFile = Join-Path $backupFull $relative
                if (-not (Test-Path -LiteralPath $backupFile -PathType Leaf)) { throw "Rollback data is missing for '$relative'." }
                $backupParent = Split-Path -Parent $backupFile
                $backupWalk = $backupFull
                $backupRelativeParent = $backupParent.Substring($backupFull.Length).TrimStart('\', '/')
                foreach ($part in ($backupRelativeParent -split '[\\/]+')) {
                    if ([string]::IsNullOrWhiteSpace($part) -or $part -eq '.') { continue }
                    $backupWalk = Join-Path $backupWalk $part
                    if (-not (Test-Path -LiteralPath $backupWalk -PathType Container)) { throw "Rollback directory is missing or invalid: '$backupWalk'." }
                    $backupParentItem = Get-Item -LiteralPath $backupWalk -Force
                    if (($backupParentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to read rollback data through a junction/symlink: '$backupWalk'." }
                }
                $backupFileInfo = Get-Item -LiteralPath $backupFile -Force
                if (($backupFileInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Rollback data is a reparse point: '$relative'." }
                if ([string]$entry.backup_sha256 -notmatch '^[0-9a-f]{64}$' -or
                    (Get-Sha256 $backupFile) -cne [string]$entry.backup_sha256) {
                    throw "Rollback data failed SHA-256 verification for '$relative'."
                }
            }
            $destination = Join-Path $rootFull $relative
            $parent = Split-Path -Parent $destination
            $walk = $rootFull
            $relativeParent = $parent.Substring($rootFull.Length).TrimStart('\', '/')
            foreach ($part in ($relativeParent -split '[\\/]+')) {
                if ([string]::IsNullOrWhiteSpace($part) -or $part -eq '.') { continue }
                $walk = Join-Path $walk $part
                if (Test-Path -LiteralPath $walk) {
                    $parentItem = Get-Item -LiteralPath $walk -Force
                    if (($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to restore through a junction/symlink: '$walk'." }
                    if (-not $parentItem.PSIsContainer) { throw "A file blocks required recovery directory '$walk'." }
                }
            }
            if (Test-Path -LiteralPath $destination) {
                $destinationItem = Get-Item -LiteralPath $destination -Force
                if ($destinationItem.PSIsContainer -or (($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
                    throw "Refusing to replace a directory or reparse point at '$destination'."
                }
            }
        }
        foreach ($requiredPath in $requiredPaths) {
            if (-not $seenPaths.ContainsKey($requiredPath)) { throw "Rollback manifest is missing required file '$requiredPath'." }
        }

        if ($state -eq 'awaiting_health' -and
            (Test-InstalledRuntimeHealth ([int]$status.runtime_port) ([string]$status.target_version) $rootFull)) {
            # The target process may have passed health just before the helper
            # was terminated. Finalize rather than rolling back a healthy update.
            Write-RecoveryStatus $status 'succeeded' 'The update helper stopped after the new runtime became healthy. The installed target version was retained.' $backupFull
            Write-UpdateLog 'INFO' "update.recovery_finalized tag=$($status.tag_name) target=$($status.target_version)"
            return
        }

        if (Test-InstallMatchesRollbackManifest $entries $backupFull $rootFull) {
            $unchangedState = if ($state -eq 'rollback_failed') { 'rolled_back' } else { 'failed' }
            $unchangedMessage = if ($unchangedState -eq 'rolled_back') {
                'The installation already matches the verified pre-update backup. No application files needed restoration.'
            } else {
                'The update helper stopped before changing application files. The existing installation matches the verified backup and was left intact.'
            }
            Write-RecoveryStatus $status $unchangedState $unchangedMessage $backupFull
            Write-UpdateLog 'INFO' "update.recovery_no_file_changes tag=$($status.tag_name) state=$state"
            return
        }

        $portForRecovery = [int]$status.runtime_port
        if ($portForRecovery -lt 1 -or $portForRecovery -gt 65535) {
            $configPath = Join-Path $dataFull 'config/engines.json'
            if (Test-Path -LiteralPath $configPath -PathType Leaf) {
                $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json -ErrorAction Stop
                $portForRecovery = [int]$config.server.port
            }
        }
        Stop-InstalledRuntimeForRecovery $rootFull $portForRecovery

        foreach ($entry in $entries) {
            $relative = [string]$entry.path
            $destination = Join-Path $rootFull $relative
            if ($entry.existed_before) {
                $backupFile = Join-Path $backupFull $relative
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
                Copy-Item -LiteralPath $backupFile -Destination $destination -Force
                if ((Get-Sha256 $destination) -cne [string]$entry.backup_sha256) {
                    throw "Restored file did not match its verified backup: '$relative'."
                }
            } elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
                Remove-Item -LiteralPath $destination -Force
            }
        }

        $restoredVersion = (& (Join-Path $rootFull 'local-ai-runtime.exe') --version | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $restoredVersion -cne [string]$status.previous_version) {
            throw "The restored runtime reports '$restoredVersion', expected '$($status.previous_version)'."
        }
        Write-RecoveryStatus $status 'rolled_back' "An interrupted update was detected. The verified backup was restored to version $restoredVersion. Start-up will continue with the restored files." $backupFull
        Write-UpdateLog 'WARN' "update.interrupted_recovered tag=$($status.tag_name) restored=$restoredVersion backup=$backupFull"
    } catch {
        $failure = [string]$_.Exception.Message
        Write-UpdateLog 'CRITICAL' "update.interrupted_recovery_failed tag=$($status.tag_name) message=$failure backup=$backupFull"
        Write-RecoveryStatus $status 'rollback_failed' "Automatic recovery could not complete. Preserve the backup at '$backupFull'. Error: $failure" $backupFull
        throw "Automatic update recovery failed. Do not delete the update stage or rollback folder. $failure"
    }
}

if ($RecoverInterrupted) {
    Invoke-RecoverInterrupted
    return
}

try {
    # Validate-only checks never stop or restart a runtime, so a live PID is
    # required only for an actual apply attempt. Keeping this distinction lets
    # CI verify tamper rejection before it starts the isolated test runtime.
    $runtimePidInvalid = (-not $ValidateOnly.IsPresent) -and ($RuntimePid -lt 1)
    if ([string]::IsNullOrWhiteSpace($PackagePath) -or [string]::IsNullOrWhiteSpace($ManifestPath) -or
        [string]::IsNullOrWhiteSpace($TagName) -or [string]::IsNullOrWhiteSpace($AssetName) -or
        [string]::IsNullOrWhiteSpace($ExpectedSha256) -or $runtimePidInvalid -or
        $Port -lt 1 -or $Port -gt 65535 -or [string]::IsNullOrWhiteSpace($CurrentVersion)) {
        throw 'Required apply parameters are missing or invalid.'
    }
    if ($TagName -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$') { throw 'The release tag is invalid.' }
    Write-UpdateStatus 'applying' 'The verified update was accepted. Revalidating the archive before changing application files.' ''
    if ($AssetName -cne 'local-ai-runtime-windows-x64.zip') { throw 'The package does not match Windows x64.' }
    if ($ExpectedSha256 -notmatch '^[0-9a-f]{64}$') { throw 'The expected SHA-256 digest is invalid.' }

    $rootFull = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
    $dataFull = [System.IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
    if ([System.IO.Path]::GetPathRoot($rootFull).TrimEnd('\') -eq $rootFull) { throw 'Refusing to update a drive root.' }
    if ($rootFull.Equals($dataFull, [StringComparison]::OrdinalIgnoreCase) -or
        $rootFull.StartsWith($dataFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $dataFull.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The application folder and user-data folder must be separate.'
    }
    $rootItem = Get-Item -LiteralPath $rootFull -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Refusing to update an application directory that is a symbolic link or junction.' }
    if (-not (Test-Path -LiteralPath (Join-Path $rootFull 'local-ai-runtime.exe') -PathType Leaf)) { throw 'The current portable runtime executable was not found.' }

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($manifest.verified -ne $true -or
        $manifest.tag_name -cne $TagName -or $manifest.asset_name -cne $AssetName -or
        ([string]$manifest.sha256).ToLowerInvariant() -cne $ExpectedSha256 -or
        [int64]$manifest.size_bytes -ne (Get-Item -LiteralPath $PackagePath).Length) {
        throw 'The staged update manifest does not match the requested release.'
    }
    if ((Get-Sha256 $PackagePath) -cne $ExpectedSha256) { throw 'The package SHA-256 changed after staging; refusing to apply.' }

    $previousVersion = (& (Join-Path $rootFull 'local-ai-runtime.exe') --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $previousVersion -cne $CurrentVersion) {
        throw "The running executable version '$previousVersion' does not match the launcher version '$CurrentVersion'."
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($PackagePath)
    [long]$expandedBytes = 0
    $seenPaths = @{}
    try {
        if ($zip.Entries.Count -gt 512) { throw 'The update archive contains too many entries.' }
        foreach ($entry in $zip.Entries) {
            $rawName = [string]$entry.FullName
            if ([string]::IsNullOrWhiteSpace($rawName) -or $rawName.StartsWith('/') -or
                $rawName.StartsWith('\') -or $rawName.Contains(':') -or $rawName.Contains('\')) {
                throw "Unsafe ZIP entry path: '$rawName'."
            }
            $isDirectory = $rawName.EndsWith('/')
            $trimmed = $rawName.TrimEnd('/')
            $parts = $trimmed.Split('/')
            if ($parts.Count -eq 0 -or @($parts | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' }).Count -gt 0) {
                throw "Unsafe ZIP entry path: '$rawName'."
            }
            $unixMode = (($entry.ExternalAttributes -shr 16) -band 0xF000)
            if ($unixMode -eq 0xA000) { throw "Symbolic links are not accepted in runtime update archives: '$rawName'." }
            $relative = $trimmed.Replace('/', '\')
            if ($isDirectory) {
                $allowedDirectory = ($relative -in @('web', 'config', 'scripts')) -or
                    $relative.StartsWith('web\', [StringComparison]::OrdinalIgnoreCase)
                if (-not $allowedDirectory) { throw "Unexpected directory in runtime update package: '$relative'." }
                continue
            }
            $allowed = ($relative -match '^local-ai-runtime\.exe$') -or
                ($relative -match '^[^\\]+\.dll$') -or
                ($relative -in @('LICENSE', 'README-INSTALL.txt', 'Start-LocalAIRuntime.cmd', 'config\engines.example.json',
                                  'scripts\Start-LocalAIRuntime.ps1', 'scripts\Apply-LocalAIRuntimeUpdate.ps1')) -or
                $relative.StartsWith('web\', [StringComparison]::OrdinalIgnoreCase)
            if (-not $allowed) { throw "Unexpected path in runtime update package: '$relative'." }
            if ($seenPaths.ContainsKey($relative)) { throw "Duplicate/case-colliding ZIP entry path: '$relative'." }
            $seenPaths[$relative] = $true
            if ($entry.Length -gt 64MB) { throw "ZIP entry is too large: '$relative'." }
            $expandedBytes += [long]$entry.Length
            if ($expandedBytes -gt 128MB) { throw 'The update archive expands beyond the 128 MiB safety limit.' }
            $incomingFiles += [pscustomobject]@{ path = $relative; existed_before = $false; backup_sha256 = '' }
        }
    } finally { $zip.Dispose() }

    if (@($incomingFiles | Where-Object { $_.path -ceq 'local-ai-runtime.exe' }).Count -ne 1 -or
        @($incomingFiles | Where-Object { $_.path -ceq 'web\index.html' }).Count -ne 1 -or
        @($incomingFiles | Where-Object { $_.path -ceq 'scripts\Start-LocalAIRuntime.ps1' }).Count -ne 1 -or
        @($incomingFiles | Where-Object { $_.path -ceq 'scripts\Apply-LocalAIRuntimeUpdate.ps1' }).Count -ne 1) {
        throw 'The archive does not contain the expected runtime, dashboard, launcher and update helper.'
    }

    $extractRoot = Join-Path $StageRoot ('extract-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    [System.IO.Compression.ZipFile]::ExtractToDirectory($PackagePath, $extractRoot)
    $candidateExe = Join-Path $extractRoot 'local-ai-runtime.exe'
    $actualTargetVersion = (& $candidateExe --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualTargetVersion -cne $installedVersion) {
        throw "The extracted executable reports '$actualTargetVersion', expected '$installedVersion'."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $extractRoot 'config/engines.example.json') -PathType Leaf)) {
        throw 'The release package is missing its example configuration.'
    }

    if ($ValidateOnly) {
        Write-UpdateLog 'INFO' "update.package_validation_succeeded tag=$TagName sha256=$ExpectedSha256 entries=$($incomingFiles.Count) expanded_bytes=$expandedBytes"
        Write-UpdateStatus 'validated' 'The staged package hash, archive paths, runtime version and required files passed validation. No application files were changed.' ''
        return
    }

    # Check write access to both the installation directory and its parent
    # before stopping the live runtime. Portable installs under protected
    # folders must fail safely rather than stop and then discover an ACL error.
    foreach ($probeDirectory in @($rootFull, (Split-Path -Parent $rootFull))) {
        $probePath = Join-Path $probeDirectory ('.lar-update-write-probe-' + [Guid]::NewGuid().ToString('N'))
        try {
            [System.IO.File]::WriteAllText($probePath, 'probe', [System.Text.UTF8Encoding]::new($false))
            [System.IO.File]::Delete($probePath)
        } catch {
            if (Test-Path -LiteralPath $probePath -PathType Leaf) { Remove-Item -LiteralPath $probePath -Force -ErrorAction SilentlyContinue }
            throw "The application folder or its parent is not writable by this user. No runtime was stopped and no application files were changed. Move the portable folder to a writable location, or update it manually. Details: $($_.Exception.Message)"
        }
    }

    $newRoot = Join-Path (Split-Path -Parent $rootFull) ((Split-Path -Leaf $rootFull) + '.update-new-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $newRoot | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $extractRoot -Force) {
        $preparedDestination = Join-Path $newRoot $item.Name
        Copy-Item -LiteralPath $item.FullName -Destination $preparedDestination -Recurse -Force
    }
    $preparedExe = Join-Path $newRoot 'local-ai-runtime.exe'
    if ((& $preparedExe --version | Out-String).Trim() -cne $installedVersion) { throw 'The prepared update copy failed its executable version check.' }

    $backupRoot = Join-Path $StageRoot ('rollback-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    foreach ($entry in $incomingFiles) {
        $destination = Join-Path $rootFull $entry.path
        $source = Join-Path $newRoot $entry.path
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Prepared update file is missing: '$($entry.path)'." }
        $parent = Split-Path -Parent $destination
        $walk = $rootFull
        $relativeParent = $parent.Substring($rootFull.Length).TrimStart('\', '/')
        foreach ($part in ($relativeParent -split '[\\/]+')) {
            if ([string]::IsNullOrWhiteSpace($part) -or $part -eq '.') { continue }
            $walk = Join-Path $walk $part
            if (Test-Path -LiteralPath $walk) {
                $parentItem = Get-Item -LiteralPath $walk -Force
                if (($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to write through a junction/symlink: '$walk'." }
                if (-not $parentItem.PSIsContainer) { throw "A file blocks required update directory '$walk'." }
            }
        }
        if (Test-Path -LiteralPath $destination) {
            $existing = Get-Item -LiteralPath $destination -Force
            if ($existing.PSIsContainer -or (($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
                throw "Refusing to replace a directory or reparse point at '$destination'."
            }
            $backupDestination = Join-Path $backupRoot $entry.path
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupDestination) | Out-Null
            Copy-Item -LiteralPath $destination -Destination $backupDestination -Force
            $entry.existed_before = $true
            $entry.backup_sha256 = Get-Sha256 $backupDestination
        } else {
            $entry.existed_before = $false
            $entry.backup_sha256 = ''
        }
    }
    $rollbackManifest = Join-Path $backupRoot 'rollback-manifest.json'
    $rollbackJson = ConvertTo-Json -InputObject @($incomingFiles) -Depth 4
    [System.IO.File]::WriteAllText($rollbackManifest, $rollbackJson, [System.Text.UTF8Encoding]::new($false))
    Write-UpdateStatus 'stopping' 'The verified package and rollback manifest are ready. The runtime is being stopped before any application files are replaced.' $backupRoot

    # Ask the running app to stop normally, which also stops managed engines.
    Start-Sleep -Milliseconds 800
    $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $RuntimePid" -ErrorAction Stop
    if (-not $processInfo -or [IO.Path]::GetFullPath($processInfo.ExecutablePath) -ine (Join-Path $rootFull 'local-ai-runtime.exe')) {
        throw 'The process on which this update was requested no longer matches the portable runtime. No application files were changed.'
    }
    try {
        $null = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/v1/runtime/shutdown" -Method Post -ContentType 'application/json' -Body '{}' -TimeoutSec 3 -ErrorAction Stop
    } catch {
        # The server may close the connection while its shutdown response is in
        # flight. The PID check below decides whether shutdown actually worked.
        if (Get-Process -Id $RuntimePid -ErrorAction SilentlyContinue) {
            throw "The runtime shutdown request failed before the process exited: $($_.Exception.Message)"
        }
    }
    $stopped = $false
    for ($attempt = 0; $attempt -lt 80; $attempt++) {
        if (-not (Get-Process -Id $RuntimePid -ErrorAction SilentlyContinue)) { $stopped = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $stopped) { throw 'The running runtime did not exit gracefully within 20 seconds; no files were changed.' }

    # Persist a recoverable state before the first replacement. If this
    # PowerShell process is killed or the machine loses power, the launcher can
    # restore the verified backup before starting the runtime again.
    Write-UpdateStatus 'replacing' 'The old runtime stopped. Application files are being replaced; the launcher can restore the saved backup if this operation is interrupted.' $backupRoot
    $filesModified = $true
    foreach ($entry in $incomingFiles) {
        $destination = Join-Path $rootFull $entry.path
        $source = Join-Path $newRoot $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $installedCheck = (& (Join-Path $rootFull 'local-ai-runtime.exe') --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $installedCheck -cne $installedVersion) { throw 'The installed runtime failed its post-copy version check.' }
    Write-UpdateStatus 'awaiting_health' 'Application files were replaced. Waiting for the new runtime health check; interrupted startup will trigger backup recovery.' $backupRoot
    if ($TestCrashAfterReplace) {
        # Deliberately terminate the isolated helper without running catch/finally.
        # This is only used by the Windows CI recovery test.
        [System.Environment]::Exit(86)
    }
    if ($TestForceFailureAfterReplace) { throw 'Intentional test-only failure after file replacement to verify rollback handling.' }
    $newRuntime = Start-Runtime $installedVersion
    if (-not (Wait-Health $Port $installedVersion 30)) { throw 'The updated runtime did not become healthy within 30 seconds.' }

    Write-UpdateLog 'INFO' "update.apply_succeeded from=$previousVersion to=$installedVersion backup=$backupRoot"
    Write-UpdateStatus 'succeeded' 'The new runtime passed its local health check. The rollback copy was retained.' $backupRoot
} catch {
    $failure = [string]$_.Exception.Message
    Write-UpdateLog 'ERROR' "update.apply_failed message=$failure"
    if ($newRuntime -and -not $newRuntime.HasExited) {
        try { Stop-Process -Id $newRuntime.Id -Force -ErrorAction SilentlyContinue } catch { }
        try { Wait-Process -Id $newRuntime.Id -Timeout 5 -ErrorAction SilentlyContinue } catch { }
    }
    if ($filesModified) {
        try {
            Restore-PreviousFiles
            $restoredVersion = (& (Join-Path $InstallRoot 'local-ai-runtime.exe') --version | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $restoredVersion -cne $previousVersion) { throw 'The restored runtime version did not match the pre-update version.' }
            $rollbackRuntime = Start-Runtime $previousVersion
            if (-not (Wait-Health $Port $previousVersion 30)) {
                if ($rollbackRuntime -and -not $rollbackRuntime.HasExited) { Stop-Process -Id $rollbackRuntime.Id -Force -ErrorAction SilentlyContinue }
                throw 'Previous files were restored, but the previous runtime did not pass its health check.'
            }
            Write-UpdateLog 'WARN' "update.rollback_succeeded restored=$previousVersion backup=$backupRoot"
            Write-UpdateStatus 'rolled_back' "Update failed and the previous runtime was restored. Reason: $failure" $backupRoot
        } catch {
            Write-UpdateLog 'CRITICAL' "update.rollback_failed message=$($_.Exception.Message) backup=$backupRoot"
            Write-UpdateStatus 'rollback_failed' "Automatic rollback encountered an error. Preserve the backup at '$backupRoot'. Original update error: $failure; rollback error: $($_.Exception.Message)" $backupRoot
        }
    } else {
        Write-UpdateStatus 'failed' "Update was not applied; current application files remain unchanged. Reason: $failure" ''
    }
} finally {
    if ($extractRoot -and (Test-Path -LiteralPath $extractRoot)) { Remove-Item -LiteralPath $extractRoot -Recurse -Force -ErrorAction SilentlyContinue }
    if ($newRoot -and (Test-Path -LiteralPath $newRoot)) { Remove-Item -LiteralPath $newRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
