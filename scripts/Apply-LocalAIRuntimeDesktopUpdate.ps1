[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$DataRoot,
    [Parameter(Mandatory = $true)][string]$StageRoot,
    [string]$InstallerPath = '',
    [string]$ManifestPath = '',
    [string]$TagName = '',
    [string]$ExpectedSha256 = '',
    [string]$CurrentVersion = '',
    [int]$DesktopPid = 0,
    [int]$Port = 0,
    [switch]$RecoverInterrupted,
    # Isolated Windows CI fault injection. Never supplied by the desktop UI.
    [switch]$TestForceFailureAfterInstall,
    [switch]$TestCrashAfterInstall,
    [switch]$TestSkipHealthCheck,
    [switch]$TestNoRestart
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$installFull = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$dataFull = [System.IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
$stageFull = [System.IO.Path]::GetFullPath($StageRoot).TrimEnd('\')
$statusPath = Join-Path $stageFull 'desktop-update-status.json'
$logDirectory = Join-Path $dataFull 'logs'
$logPath = Join-Path $logDirectory 'desktop-update.log'
$backupRoot = ''
$previousVersion = ''
$targetVersion = $TagName -replace '^v', ''
$startedDesktop = $null
$shutdownStarted = $false
$installerPid = 0

function Write-UpdateLog([string]$Level, [string]$Message) {
    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    Add-Content -LiteralPath $logPath -Value ('{0} {1} {2}' -f [DateTimeOffset]::UtcNow.ToString('o'), $Level, $Message) -Encoding UTF8
}

function Write-UpdateStatus([string]$State, [string]$Message, [string]$Backup = '') {
    $record = [ordered]@{
        generated_utc = [DateTimeOffset]::UtcNow.ToString('o')
        state = $State
        tag_name = $TagName
        previous_version = $previousVersion
        target_version = $targetVersion
        install_root = $installFull
        data_root = $dataFull
        backup_path = $Backup
        helper_pid = $PID
        desktop_pid = $DesktopPid
        installer_pid = $installerPid
        runtime_port = $Port
        message = $Message
    }
    $temporary = "$statusPath.part"
    [System.IO.File]::WriteAllText($temporary, ($record | ConvertTo-Json -Depth 5), [System.Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temporary -Destination $statusPath -Force
}

function Get-Sha256([string]$Path) {
    $stream = $null
    $sha = $null
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        $sha = [System.Security.Cryptography.SHA256]::Create()
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        if ($null -ne $stream) { $stream.Dispose() }
        if ($null -ne $sha) { $sha.Dispose() }
    }
}

function Get-ProcessInfo([int]$ProcessId) {
    if ($ProcessId -le 0) { return $null }
    try {
        return Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction Stop
    } catch {
        if (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
            throw "Windows could not verify the executable identity for active PID ${ProcessId}: $($_.Exception.Message)"
        }
        return $null
    }
}

function Test-HelperStillRunning($Status, [string]$ExpectedScript) {
    $helperId = 0
    try { $helperId = [int]$Status.helper_pid } catch { return $false }
    if ($helperId -le 0 -or $helperId -eq $PID) { return $false }
    $info = Get-ProcessInfo $helperId
    if (-not $info -or [string]$info.Name -notmatch '^(?i:powershell|pwsh)\.exe$') { return $false }
    return ([string]$info.CommandLine).IndexOf([System.IO.Path]::GetFullPath($ExpectedScript), [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function Assert-PathLayout {
    if ([System.IO.Path]::GetPathRoot($installFull).TrimEnd('\') -eq $installFull) { throw 'Refusing to update a drive root.' }
    if ($installFull.Equals($dataFull, [StringComparison]::OrdinalIgnoreCase) -or
        $installFull.StartsWith($dataFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $dataFull.StartsWith($installFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The installed application directory and user-data directory must be separate.'
    }
    $updatesFull = [System.IO.Path]::GetFullPath((Join-Path $dataFull 'updates')).TrimEnd('\')
    if (-not $stageFull.StartsWith($updatesFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not [System.IO.Path]::GetDirectoryName($stageFull).Equals($updatesFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The desktop update stage must be a direct release-tag child of the user-data updates folder.'
    }
    if ((Split-Path -Leaf $stageFull) -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$') { throw 'The update stage folder does not have a valid release-tag name.' }
    foreach ($path in @($installFull, $dataFull, $updatesFull, $stageFull)) {
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Required update directory is missing: $path" }
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to use a symbolic link or junction: $path" }
    }
}

function Wait-ForProcessExit([int]$ProcessId, [string]$ExpectedExecutable, [int]$TimeoutSeconds) {
    if ($ProcessId -le 0) { return }
    $info = Get-ProcessInfo $ProcessId
    if (-not $info) { return }
    if (-not $info.ExecutablePath -or -not [System.IO.Path]::GetFullPath([string]$info.ExecutablePath).Equals(
        [System.IO.Path]::GetFullPath($ExpectedExecutable), [StringComparison]::OrdinalIgnoreCase)) {
        throw "PID $ProcessId is not the expected Local AI Runtime desktop process. Refusing to wait on or stop an unrelated process."
    }
    for ($attempt = 0; $attempt -lt ($TimeoutSeconds * 4); $attempt++) {
        if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { return }
        Start-Sleep -Milliseconds 250
    }
    throw "The Local AI Runtime desktop window (PID $ProcessId) did not exit within $TimeoutSeconds seconds. No installer was started."
}

function Stop-InstalledRuntime([int]$ExpectedPort) {
    $expectedExe = [System.IO.Path]::GetFullPath((Join-Path $installFull 'local-ai-runtime.exe'))
    $processes = @(Get-CimInstance Win32_Process -Filter "Name = 'local-ai-runtime.exe'" -ErrorAction Stop |
        Where-Object { $_.ExecutablePath -and [System.IO.Path]::GetFullPath([string]$_.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase) })
    foreach ($runtime in $processes) {
        $runtimeId = [int]$runtime.ProcessId
        try {
            $null = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$ExpectedPort/v1/runtime/shutdown" `
                -Method Post -ContentType 'application/json' -Body '{}' -TimeoutSec 4 -ErrorAction Stop
        } catch {
            if (Get-Process -Id $runtimeId -ErrorAction SilentlyContinue) {
                throw "The installed runtime did not confirm graceful shutdown: $($_.Exception.Message)"
            }
        }
        for ($attempt = 0; $attempt -lt 120; $attempt++) {
            if (-not (Get-Process -Id $runtimeId -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 250
        }
        if (Get-Process -Id $runtimeId -ErrorAction SilentlyContinue) {
            throw "The installed runtime PID $runtimeId did not stop gracefully. No application files were changed."
        }
    }
}

function Get-SafeInstallFiles([string]$Root) {
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $files = @()
    foreach ($item in @(Get-ChildItem -LiteralPath $rootFull -Force -Recurse -ErrorAction Stop)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to back up an installation containing a symbolic link or junction: $($item.FullName)"
        }
        if ($item.PSIsContainer) { continue }
        $relative = $item.FullName.Substring($rootFull.Length).TrimStart('\', '/')
        if ([string]::IsNullOrWhiteSpace($relative) -or $relative.Contains(':')) { throw "Unsafe installed file path: $relative" }
        $files += [pscustomobject]@{ path = $relative; length = [int64]$item.Length; sha256 = (Get-Sha256 $item.FullName) }
    }
    if ($files.Count -lt 4 -or $files.Count -gt 4096) { throw "Unexpected installed file count for backup: $($files.Count)." }
    return ,$files
}

function New-VerifiedBackup {
    # Hash the source tree before allocating anything. The backup and installer
    # must fit on the user-data volume before the desktop/runtime are stopped.
    $entries = Get-SafeInstallFiles $installFull
    $totalBytes = [int64](($entries | Measure-Object -Property length -Sum).Sum)
    $installerBytes = [int64](Get-Item -LiteralPath $InstallerPath).Length
    $requiredBytes = $totalBytes + $installerBytes + (64MB)
    $driveRoot = [System.IO.Path]::GetPathRoot($stageFull)
    $drive = [System.IO.DriveInfo]::new($driveRoot)
    if ($drive.AvailableFreeSpace -lt $requiredBytes) {
        throw "Insufficient free space for a full desktop rollback backup. Required at least $requiredBytes bytes; available $($drive.AvailableFreeSpace) bytes."
    }

    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
    $backup = Join-Path $stageFull ('rollback-' + $stamp + '-' + [Guid]::NewGuid().ToString('N'))
    try {
        $filesRoot = Join-Path $backup 'files'
        New-Item -ItemType Directory -Force -Path $filesRoot | Out-Null
        foreach ($entry in $entries) {
            $source = Join-Path $installFull $entry.path
            $destination = Join-Path $filesRoot $entry.path
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination -Force
            if ((Get-Sha256 $destination) -cne [string]$entry.sha256) { throw "Backup copy failed SHA-256 verification for '$($entry.path)'." }
        }
        [System.IO.File]::WriteAllText((Join-Path $backup 'backup-manifest.json'),
            (ConvertTo-Json -InputObject @($entries) -Depth 4), [System.Text.UTF8Encoding]::new($false))
        return $backup
    } catch {
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }
}

function Assert-InstalledDesktopPayload([string]$ExpectedVersion) {
    $runtimeExe = Join-Path $installFull 'local-ai-runtime.exe'
    $desktopExe = Join-Path $installFull 'LocalAIRuntime.Desktop.exe'
    $restoredVersion = (& $runtimeExe --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $restoredVersion -cne $ExpectedVersion) {
        throw "The installed runtime reports '$restoredVersion', expected '$ExpectedVersion'."
    }
    $verify = Start-Process -FilePath $desktopExe -ArgumentList @('--verify-install') -WorkingDirectory $installFull -Wait -PassThru -WindowStyle Hidden
    if ($verify.ExitCode -ne 0) { throw 'The installed desktop shell failed its payload verification.' }
}

function Restore-VerifiedBackup([string]$Backup) {
    $backupFull = [System.IO.Path]::GetFullPath($Backup).TrimEnd('\')
    if (-not $backupFull.StartsWith($stageFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not [System.IO.Path]::GetDirectoryName($backupFull).Equals($stageFull, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $backupFull) -notmatch '^rollback-\d{8}T\d{6}Z-[0-9a-fA-F]{32}$') {
        throw 'The desktop update backup path is missing or outside its release stage.'
    }
    if (((Get-Item -LiteralPath $backupFull -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Refusing to restore a desktop backup directory that is a symbolic link or junction.'
    }
    $manifestPath = Join-Path $backupFull 'backup-manifest.json'
    $filesRoot = Join-Path $backupFull 'files'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or -not (Test-Path -LiteralPath $filesRoot -PathType Container)) {
        throw 'The desktop update backup manifest or files folder is missing.'
    }
    foreach ($protectedPath in @($manifestPath, $filesRoot)) {
        if (((Get-Item -LiteralPath $protectedPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to restore a desktop backup component that is a symbolic link: $protectedPath"
        }
    }
    foreach ($backupItem in @(Get-ChildItem -LiteralPath $backupFull -Force -Recurse -ErrorAction Stop)) {
        if (($backupItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to restore a backup containing a symbolic link or junction: $($backupItem.FullName)"
        }
    }
    $entries = @(Get-Content -LiteralPath $manifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop)
    if ($entries.Count -lt 4 -or $entries.Count -gt 4096) { throw 'The desktop update backup manifest has an invalid file count.' }
    $seenPaths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $entries) {
        $relative = [string]$entry.path
        if ([string]::IsNullOrWhiteSpace($relative) -or $relative.StartsWith('\') -or $relative.Contains(':') -or $relative.Contains('/') -or
            @($relative.Split('\') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0) {
            throw "Unsafe path in desktop update backup manifest: '$relative'."
        }
        if (-not $seenPaths.Add($relative)) { throw "Duplicate path in desktop update backup manifest: '$relative'." }
        $source = Join-Path $filesRoot $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or
            (Get-Item -LiteralPath $source -Force).Length -ne [int64]$entry.length -or
            (Get-Sha256 $source) -cne [string]$entry.sha256) {
            throw "Desktop update backup failed integrity verification for '$relative'."
        }
    }

    foreach ($item in @(Get-ChildItem -LiteralPath $installFull -Force -Recurse -ErrorAction Stop)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clear an install folder containing a reparse point: $($item.FullName)" }
    }
    Get-ChildItem -LiteralPath $installFull -Force | Remove-Item -Recurse -Force
    foreach ($entry in $entries) {
        $destination = Join-Path $installFull ([string]$entry.path)
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $filesRoot ([string]$entry.path)) -Destination $destination -Force
        if ((Get-Sha256 $destination) -cne [string]$entry.sha256) { throw "Restored desktop file failed SHA-256 verification: '$($entry.path)'." }
    }
}

function Test-InstalledHealth([int]$ExpectedPort, [string]$ExpectedVersion) {
    try {
        $health = Invoke-RestMethod -UseBasicParsing -Uri "http://127.0.0.1:$ExpectedPort/health" -TimeoutSec 1 -ErrorAction Stop
        if ($health.status -ne 'ok' -or $health.version -ne $ExpectedVersion) { return $false }

        # A healthy endpoint on the expected port is not sufficient: an older
        # runtime from another folder or data root must never satisfy recovery.
        $identityPath = Join-Path $dataFull 'runtime-instance.id'
        if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf) -or
            ((Get-Item -LiteralPath $identityPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            return $false
        }
        $expectedIdentity = (Get-Content -LiteralPath $identityPath -Raw -ErrorAction Stop).Trim()
        $expectedGuid = [Guid]::Empty
        if (-not [Guid]::TryParse($expectedIdentity, [ref]$expectedGuid)) { return $false }
        $identity = Invoke-RestMethod -UseBasicParsing -Uri "http://127.0.0.1:$ExpectedPort/v1/system/identity" -TimeoutSec 1 -ErrorAction Stop
        if ([int]$identity.schema_version -ne 1 -or [string]$identity.data_root_id -cne $expectedGuid.ToString()) { return $false }

        $expectedExe = [System.IO.Path]::GetFullPath((Join-Path $installFull 'local-ai-runtime.exe'))
        foreach ($listener in @(Get-NetTCPConnection -LocalPort $ExpectedPort -State Listen -ErrorAction Stop)) {
            $owner = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction SilentlyContinue
            if ($owner -and $owner.ExecutablePath -and [System.IO.Path]::GetFullPath([string]$owner.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
    } catch { }
    return $false
}

function Start-InstalledDesktop {
    $desktopExe = Join-Path $installFull 'LocalAIRuntime.Desktop.exe'
    if (-not (Test-Path -LiteralPath $desktopExe -PathType Leaf)) { throw 'The installed desktop host is missing after setup.' }
    $previousHelperPid = $env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_HELPER_PID
    $previousStage = $env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_STAGE
    try {
        $env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_HELPER_PID = [string]$PID
        $env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_STAGE = $stageFull
        return Start-Process -FilePath $desktopExe -WorkingDirectory $installFull -PassThru
    } finally {
        if ($null -eq $previousHelperPid) { Remove-Item Env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_HELPER_PID -ErrorAction SilentlyContinue }
        else { $env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_HELPER_PID = $previousHelperPid }
        if ($null -eq $previousStage) { Remove-Item Env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_STAGE -ErrorAction SilentlyContinue }
        else { $env:LOCAL_AI_RUNTIME_DESKTOP_UPDATE_STAGE = $previousStage }
    }
}

function Stop-StartedDesktop($Process) {
    if ($null -eq $Process) { return }
    try {
        $info = Get-ProcessInfo ([int]$Process.Id)
        $expected = [System.IO.Path]::GetFullPath((Join-Path $installFull 'LocalAIRuntime.Desktop.exe'))
        if ($info -and $info.ExecutablePath -and [System.IO.Path]::GetFullPath([string]$info.ExecutablePath).Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            try { Wait-Process -Id $Process.Id -Timeout 10 -ErrorAction SilentlyContinue } catch { }
        }
    } catch { }
}

function Wait-InstalledHealth([int]$ExpectedPort, [string]$ExpectedVersion, [int]$Seconds) {
    for ($attempt = 0; $attempt -lt ($Seconds * 4); $attempt++) {
        if (Test-InstalledHealth $ExpectedPort $ExpectedVersion) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Start-AndVerifyInstalledDesktop([string]$ExpectedVersion) {
    $desktop = Start-InstalledDesktop
    if (-not (Wait-InstalledHealth $Port $ExpectedVersion 120)) {
        Stop-StartedDesktop $desktop
        throw "The restored/updated desktop did not start a healthy runtime version '$ExpectedVersion' within 120 seconds."
    }
    $info = Get-ProcessInfo ([int]$desktop.Id)
    $expectedExe = [System.IO.Path]::GetFullPath((Join-Path $installFull 'LocalAIRuntime.Desktop.exe'))
    if (-not $info -or -not $info.ExecutablePath -or
        -not [System.IO.Path]::GetFullPath([string]$info.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)) {
        Stop-StartedDesktop $desktop
        throw 'The desktop shell exited or its process identity changed before the update health check completed.'
    }
    return $desktop
}


function Wait-ForStagedInstallerExit([int]$ExpectedProcessId, [int]$TimeoutSeconds = 600) {
    $expectedInstaller = [System.IO.Path]::GetFullPath((Join-Path $stageFull 'local-ai-runtime-windows-x64-setup.exe'))
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $active = @()
        if ($ExpectedProcessId -gt 0) {
            $candidate = Get-ProcessInfo $ExpectedProcessId
            if ($candidate -and $candidate.ExecutablePath -and
                [System.IO.Path]::GetFullPath([string]$candidate.ExecutablePath).Equals($expectedInstaller, [StringComparison]::OrdinalIgnoreCase)) {
                $active += $candidate
            }
        }
        if ($active.Count -eq 0) {
            $active = @(Get-CimInstance Win32_Process -Filter "Name = 'local-ai-runtime-windows-x64-setup.exe'" -ErrorAction Stop |
                Where-Object { $_.ExecutablePath -and [System.IO.Path]::GetFullPath([string]$_.ExecutablePath).Equals($expectedInstaller, [StringComparison]::OrdinalIgnoreCase) })
        }
        if ($active.Count -eq 0) { return }
        Start-Sleep -Seconds 1
    }
    $pidText = ($active | ForEach-Object { [string]$_.ProcessId }) -join ', '
    throw "The staged desktop setup installer is still running (PID: $pidText). Recovery will not replace files while Windows Setup may still be active."
}

function Invoke-RecoverInterrupted {
    if (-not (Test-Path -LiteralPath $statusPath -PathType Leaf)) { return }
    $statusItem = Get-Item -LiteralPath $statusPath -Force -ErrorAction Stop
    if (($statusItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The desktop update status record is a symbolic link or junction.'
    }
    $status = Get-Content -LiteralPath $statusPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if (-not [System.IO.Path]::GetFullPath([string]$status.install_root).TrimEnd('\').Equals($installFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The interrupted desktop update belongs to a different installation folder.'
    }
    if (-not [System.IO.Path]::GetFullPath([string]$status.data_root).TrimEnd('\').Equals($dataFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The interrupted desktop update belongs to a different user-data directory.'
    }
    if ([string]$status.tag_name -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$' -or
        (Split-Path -Leaf $stageFull) -cne [string]$status.tag_name -or
        [string]$status.previous_version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$' -or
        [string]$status.target_version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$' -or
        [int]$status.runtime_port -lt 1 -or [int]$status.runtime_port -gt 65535) {
        throw 'The interrupted desktop update record contains an invalid tag, version, stage or runtime port.'
    }
    $state = [string]$status.state
    if ($state -notin @('waiting_for_shutdown', 'installing', 'awaiting_health', 'rollback_failed')) { return }
    if (Test-HelperStillRunning $status (Join-Path $stageFull 'Apply-LocalAIRuntimeDesktopUpdate.ps1')) {
        Write-UpdateLog 'INFO' "update.recovery_deferred helper_pid=$($status.helper_pid) state=$state"
        return
    }

    $script:TagName = [string]$status.tag_name
    $script:targetVersion = [string]$status.target_version
    $script:previousVersion = [string]$status.previous_version
    $script:backupRoot = [string]$status.backup_path
    $script:Port = [int]$status.runtime_port

    if ($state -eq 'awaiting_health' -and (Test-InstalledHealth $Port $targetVersion)) {
        if ($DesktopPid -gt 0) { Wait-ForProcessExit $DesktopPid (Join-Path $installFull 'LocalAIRuntime.Desktop.exe') 60 }
        # Transfer the durable helper ownership to this recovery process before
        # launching the child desktop; otherwise startup would start a second
        # recovery helper while this one is waiting for the health check.
        Write-UpdateStatus 'awaiting_health' 'The target runtime is already healthy. Finalizing the interrupted desktop update and verifying the shell restart.' $backupRoot
        if ($DesktopPid -gt 0 -and -not $TestNoRestart) { $null = Start-AndVerifyInstalledDesktop $targetVersion }
        Write-UpdateStatus 'succeeded' 'The desktop update helper stopped after the new runtime passed health checks. The installed target version was retained.' $backupRoot
        Write-UpdateLog 'INFO' "update.recovery_finalized target=$targetVersion"
        return
    }
    if ([string]::IsNullOrWhiteSpace($backupRoot)) {
        if ($DesktopPid -gt 0) { Wait-ForProcessExit $DesktopPid (Join-Path $installFull 'LocalAIRuntime.Desktop.exe') 60 }
        # Mark this attempt terminal before launching the unchanged old app.
        # This prevents a new recovery process from looping on an obsolete
        # waiting_for_shutdown record that never reached file replacement.
        Write-UpdateStatus 'failed' 'The update stopped before a verified application backup was created. No installed application files were replaced.' ''
        if ($DesktopPid -gt 0 -and -not $TestNoRestart) { $null = Start-AndVerifyInstalledDesktop $previousVersion }
        return
    }
    if ($DesktopPid -gt 0) {
        Wait-ForProcessExit $DesktopPid (Join-Path $installFull 'LocalAIRuntime.Desktop.exe') 60
    }
    Wait-ForStagedInstallerExit ([int]$status.installer_pid) 600
    Stop-InstalledRuntime $Port
    Restore-VerifiedBackup $backupRoot
    Assert-InstalledDesktopPayload $previousVersion
    $restored = $previousVersion
    # Refresh the recovery-helper PID while the status remains non-terminal so
    # the shell launched below can validate this helper and avoid a recovery loop.
    Write-UpdateStatus 'installing' "The verified previous installation was restored to version $restored. Restarting the desktop shell and checking runtime health." $backupRoot
    if (-not $TestNoRestart) { $null = Start-AndVerifyInstalledDesktop $restored }
    Write-UpdateStatus 'rolled_back' "An interrupted desktop update was detected. The verified full-install backup was restored to version $restored and restarted successfully." $backupRoot
    Write-UpdateLog 'WARN' "update.interrupted_recovered restored=$restored backup=$backupRoot"
}

try {
    Assert-PathLayout
    if ($RecoverInterrupted) {
        Invoke-RecoverInterrupted
        return
    }

    if ($TagName -notmatch '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$') { throw 'The release tag is invalid.' }
    if ($ExpectedSha256 -notmatch '^[0-9a-f]{64}$') { throw 'The expected installer SHA-256 is invalid.' }
    $testMode = $TestForceFailureAfterInstall.IsPresent -or $TestCrashAfterInstall.IsPresent -or $TestSkipHealthCheck.IsPresent
    if ((-not $testMode -and $DesktopPid -lt 1) -or $Port -lt 1 -or $Port -gt 65535 -or [string]::IsNullOrWhiteSpace($CurrentVersion)) {
        throw 'The desktop PID, runtime port or current version is missing or invalid.'
    }
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf) -or -not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw 'The verified Windows setup installer or its manifest is missing.'
    }
    foreach ($filePath in @($InstallerPath, $ManifestPath)) {
        if (((Get-Item -LiteralPath $filePath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to use an installer or manifest that is a symbolic link: $filePath"
        }
    }
    if ((Split-Path -Leaf $stageFull) -cne $TagName) { throw 'The selected release tag does not match the staged directory.' }
    $installerFull = [System.IO.Path]::GetFullPath($InstallerPath)
    $manifestFull = [System.IO.Path]::GetFullPath($ManifestPath)
    if (-not $installerFull.StartsWith($stageFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $installerFull) -cne 'local-ai-runtime-windows-x64-setup.exe') {
        throw 'The setup installer must be the official desktop asset inside its isolated release stage.'
    }
    if (-not $manifestFull.StartsWith($stageFull + '\', [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $manifestFull) -cne 'desktop-update-manifest.json') {
        throw 'The update manifest must be the expected manifest inside the isolated release stage.'
    }
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($manifest.verified -ne $true -or $manifest.tag_name -cne $TagName -or
        $manifest.asset_name -cne 'local-ai-runtime-windows-x64-setup.exe' -or
        $manifest.release_url -cne ('https://github.com/azmisahin-ai/local-ai-runtime/releases/tag/' + $TagName) -or
        ([string]$manifest.sha256).ToLowerInvariant() -cne $ExpectedSha256 -or
        [int64]$manifest.size_bytes -ne (Get-Item -LiteralPath $InstallerPath).Length) {
        throw 'The staged desktop installer manifest does not match the requested official release.'
    }
    if ((Get-Sha256 $InstallerPath) -cne $ExpectedSha256) { throw 'The desktop installer SHA-256 changed after staging; refusing to execute it.' }

    $runtimeExe = Join-Path $installFull 'local-ai-runtime.exe'
    $desktopExe = Join-Path $installFull 'LocalAIRuntime.Desktop.exe'
    if (-not (Test-Path -LiteralPath $runtimeExe -PathType Leaf) -or -not (Test-Path -LiteralPath $desktopExe -PathType Leaf)) {
        throw 'The installed desktop application is incomplete. Reinstall using the official setup package.'
    }
    $previousVersion = (& $runtimeExe --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $previousVersion -cne $CurrentVersion) { throw "The installed runtime version '$previousVersion' does not match the desktop's expected version '$CurrentVersion'." }
    if ($previousVersion -eq $targetVersion -and -not ($TestSkipHealthCheck -or $TestForceFailureAfterInstall -or $TestCrashAfterInstall)) {
        throw 'The selected desktop update is not newer than the installed runtime.'
    }

    Write-UpdateStatus 'waiting_for_shutdown' 'The official installer hash is verified. Waiting for the desktop window and runtime to close before backing up or replacing any installed files.' ''
    $shutdownStarted = $true
    Stop-InstalledRuntime $Port
    Wait-ForProcessExit $DesktopPid $desktopExe 60

    $backupRoot = New-VerifiedBackup
    Write-UpdateStatus 'installing' 'The complete installed application has a verified backup. Running the official per-user setup installer.' $backupRoot

    # Re-hash after shutdown/backup and immediately before CreateProcess so a
    # staged installer changed during the hand-off can never be executed.
    if ((Get-Sha256 $InstallerPath) -cne $ExpectedSha256) {
        throw 'The desktop installer SHA-256 changed immediately before execution; refusing to run it.'
    }
    $installArgument = '/DIR="' + $installFull + '"'
    $setup = Start-Process -FilePath $InstallerPath -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', $installArgument) -WorkingDirectory $stageFull -PassThru
    $installerPid = [int]$setup.Id
    Write-UpdateStatus 'installing' 'The official setup installer is running. Its process ID is recorded so interrupted recovery never races an active installer.' $backupRoot
    $setup.WaitForExit()
    $setup.Refresh()
    if ($setup.ExitCode -ne 0) { throw "The Windows setup installer failed with exit code $($setup.ExitCode)." }
    $installerPid = 0

    $installedVersion = (& (Join-Path $installFull 'local-ai-runtime.exe') --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $installedVersion -cne $targetVersion) { throw "The installed runtime reports '$installedVersion', expected '$targetVersion'." }
    $verify = Start-Process -FilePath (Join-Path $installFull 'LocalAIRuntime.Desktop.exe') -ArgumentList @('--verify-install') `
        -WorkingDirectory $installFull -Wait -PassThru -WindowStyle Hidden
    if ($verify.ExitCode -ne 0) { throw 'The updated desktop payload failed its installation verification.' }
    foreach ($required in @('scripts/Start-LocalAIRuntime.ps1', 'scripts/Apply-LocalAIRuntimeUpdate.ps1',
                            'scripts/Apply-LocalAIRuntimeDesktopUpdate.ps1', 'web/index.html', 'web/desktop-shell.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installFull $required) -PathType Leaf)) { throw "Updated desktop payload is missing '$required'." }
    }

    Write-UpdateStatus 'awaiting_health' 'The setup installer completed and the desktop payload passed verification. Starting the desktop shell and checking the runtime health endpoint.' $backupRoot
    if ($TestCrashAfterInstall) { [System.Environment]::Exit(87) }
    if ($TestForceFailureAfterInstall) { throw 'Intentional test-only failure after installation to verify complete desktop rollback.' }
    if ($TestSkipHealthCheck) {
        Write-UpdateStatus 'succeeded' 'The setup installer completed and the updated desktop payload passed CI fixture verification. Interactive startup health was intentionally skipped by the isolated test.' $backupRoot
        Write-UpdateLog 'INFO' "update.test_apply_succeeded target=$targetVersion backup=$backupRoot"
        return
    }

    $startedDesktop = Start-AndVerifyInstalledDesktop $targetVersion
    Write-UpdateStatus 'succeeded' 'The updated desktop shell remained running and its runtime passed the local health check. The full-install rollback backup was retained.' $backupRoot
    Write-UpdateLog 'INFO' "update.apply_succeeded from=$previousVersion to=$targetVersion backup=$backupRoot"
} catch {
    $failure = [string]$_.Exception.Message
    Write-UpdateLog 'ERROR' "update.apply_failed message=$failure"
    if ($startedDesktop -and -not $startedDesktop.HasExited) { Stop-StartedDesktop $startedDesktop }
    if ($RecoverInterrupted) {
        # Recovery failures must remain recoverable. Never replace an interrupted
        # status with a terminal failed record or discard its verified backup path.
        if ($TagName -match '^v\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$' -and
            (Test-Path -LiteralPath $statusPath -PathType Leaf) -and
            ((Get-Item -LiteralPath $statusPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
            Write-UpdateStatus 'rollback_failed' "Interrupted desktop update recovery is still pending. Preserve '$backupRoot' and inspect desktop-update.log before retrying. Recovery error: $failure" $backupRoot
        }
        Write-UpdateLog 'CRITICAL' "update.interrupted_recovery_failed backup=$backupRoot message=$failure"
        return
    }
    if (-not $shutdownStarted) {
        Write-UpdateStatus 'failed' "The desktop update was rejected before the running app or installation was changed. Reason: $failure" ''
        Write-UpdateLog 'INFO' 'update.preflight_failed_without_process_changes'
        return
    }
    $runtimeStopped = $true
    try { Stop-InstalledRuntime $Port }
    catch {
        $runtimeStopped = $false
        Write-UpdateLog 'CRITICAL' "update.runtime_stop_after_failure message=$($_.Exception.Message)"
    }
    if (-not [string]::IsNullOrWhiteSpace($backupRoot) -and (Test-Path -LiteralPath $backupRoot -PathType Container)) {
        if (-not $runtimeStopped) {
            Write-UpdateStatus 'rollback_failed' "The update failed, but the installed runtime could not be confirmed stopped. The application files were not cleared for rollback; preserve '$backupRoot' and retry recovery after the runtime exits. Update error: $failure" $backupRoot
            Write-UpdateLog 'CRITICAL' "update.rollback_deferred_runtime_active backup=$backupRoot"
            return
        }
        try {
            Restore-VerifiedBackup $backupRoot
            Assert-InstalledDesktopPayload $previousVersion
            $restored = $previousVersion
            if (-not $TestNoRestart) { $null = Start-AndVerifyInstalledDesktop $restored }
            Write-UpdateStatus 'rolled_back' "The desktop update failed and the entire previous installation was restored and restarted. Reason: $failure" $backupRoot
            Write-UpdateLog 'WARN' "update.rollback_succeeded restored=$restored backup=$backupRoot"
        } catch {
            Write-UpdateStatus 'rollback_failed' "Automatic full-install rollback failed. Preserve '$backupRoot'. Update error: $failure; rollback error: $($_.Exception.Message)" $backupRoot
            Write-UpdateLog 'CRITICAL' "update.rollback_failed backup=$backupRoot message=$($_.Exception.Message)"
        }
    } else {
        Write-UpdateStatus 'failed' "The desktop update did not modify the installation. Reason: $failure" ''
        if (-not $TestNoRestart -and $DesktopPid -gt 0) {
            try {
                Wait-ForProcessExit $DesktopPid (Join-Path $installFull 'LocalAIRuntime.Desktop.exe') 60
                $null = Start-AndVerifyInstalledDesktop $previousVersion
            }
            catch { Write-UpdateLog 'ERROR' "update.restart_previous_desktop_failed message=$($_.Exception.Message)" }
        }
    }
}
