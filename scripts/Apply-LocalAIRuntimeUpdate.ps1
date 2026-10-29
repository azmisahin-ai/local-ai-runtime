[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$DataRoot,
    [Parameter(Mandatory = $true)][string]$StageRoot,
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$TagName,
    [Parameter(Mandatory = $true)][string]$AssetName,
    [Parameter(Mandatory = $true)][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][int]$RuntimePid,
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$CurrentVersion,
    [switch]$ValidateOnly,
    # Explicit test-only fault injection used by the isolated Windows CI fixture.
    # The dashboard/API never passes this switch.
    [switch]$TestForceFailureAfterReplace
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
    if (-not $incomingFiles -or -not (Test-Path -LiteralPath $backupRoot -PathType Container)) { return }
    foreach ($entry in $incomingFiles) {
        $relative = [string]$entry.path
        $destination = Join-Path $InstallRoot $relative
        if ($entry.existed_before) {
            $backup = Join-Path $backupRoot $relative
            if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
                throw "Rollback data is missing for '$relative'; the backup was not complete."
            }
            $parent = Split-Path -Parent $destination
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
            Copy-Item -LiteralPath $backup -Destination $destination -Force
        } else {
            if (Test-Path -LiteralPath $destination -PathType Leaf) {
                Remove-Item -LiteralPath $destination -Force
            }
        }
    }
}

try {
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
                ($relative -in @('LICENSE', 'README-INSTALL.txt', 'config\engines.example.json',
                                  'scripts\Start-LocalAIRuntime.ps1', 'scripts\Apply-LocalAIRuntimeUpdate.ps1')) -or
                $relative.StartsWith('web\', [StringComparison]::OrdinalIgnoreCase)
            if (-not $allowed) { throw "Unexpected path in runtime update package: '$relative'." }
            if ($seenPaths.ContainsKey($relative)) { throw "Duplicate/case-colliding ZIP entry path: '$relative'." }
            $seenPaths[$relative] = $true
            if ($entry.Length -gt 64MB) { throw "ZIP entry is too large: '$relative'." }
            $expandedBytes += [long]$entry.Length
            if ($expandedBytes -gt 128MB) { throw 'The update archive expands beyond the 128 MiB safety limit.' }
            $incomingFiles += [pscustomobject]@{ path = $relative; existed_before = $false }
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
        } else {
            $entry.existed_before = $false
        }
    }
    $incomingFiles | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backupRoot 'rollback-manifest.json') -Encoding UTF8

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

    $filesModified = $true
    foreach ($entry in $incomingFiles) {
        $destination = Join-Path $rootFull $entry.path
        $source = Join-Path $newRoot $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $installedCheck = (& (Join-Path $rootFull 'local-ai-runtime.exe') --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $installedCheck -cne $installedVersion) { throw 'The installed runtime failed its post-copy version check.' }
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
