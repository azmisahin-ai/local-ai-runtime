[CmdletBinding()]
param(
    [switch]$NoBrowser,
    [switch]$PassThru,
    [switch]$NoPrompts,
    [string]$DataRoot = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    # Keep config, model/engine downloads and logs outside the program directory
    # so upgrades work even when the runtime folder is read-only (e.g. Program Files).
    # DataRoot is an optional explicit override for isolated installs and tests.
    if ([string]::IsNullOrWhiteSpace($DataRoot)) {
        if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
            $DataRoot = Join-Path $env:LOCALAPPDATA 'LocalAIRuntime'
        } else {
            $DataRoot = Join-Path $env:TEMP 'LocalAIRuntime'
        }
    }
    $dataRoot = [System.IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
    $rootFull = [System.IO.Path]::GetFullPath($root).TrimEnd('\')
    if ($rootFull.Equals($dataRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $rootFull.StartsWith($dataRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $dataRoot.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The application folder and user-data folder must be separate.'
    }

    function Test-UpdateHelperRunning($Status, [string]$StageRootFull) {
        $helperPid = 0
        try { $helperPid = [int]$Status.helper_pid } catch { return $false }
        if ($helperPid -le 0) { return $false }
        try {
            $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $helperPid" -ErrorAction Stop
        } catch {
            throw "Could not determine whether update helper PID $helperPid is still running. Start-up is paused to avoid racing an active update."
        }
        if (-not $processInfo -or [string]$processInfo.Name -notmatch '^(?i:powershell|pwsh)\.exe$') { return $false }
        $expectedHelper = [System.IO.Path]::GetFullPath((Join-Path $StageRootFull 'Apply-LocalAIRuntimeUpdate.ps1'))
        return ([string]$processInfo.CommandLine).IndexOf($expectedHelper, [StringComparison]::OrdinalIgnoreCase) -ge 0
    }

    # A forced process exit, Windows restart or power loss can leave the
    # updater between file replacement and its post-update health check. Use
    # the isolated helper and checksum-verified pre-update backup to recover
    # before even trying to launch the runtime executable.
    $updatesRoot = Join-Path $dataRoot 'updates'
    if (Test-Path -LiteralPath $updatesRoot -PathType Container) {
        foreach ($stageDirectory in @(Get-ChildItem -LiteralPath $updatesRoot -Directory -Force | Sort-Object LastWriteTime -Descending)) {
            $statusPath = Join-Path $stageDirectory.FullName 'apply-status.json'
            if (-not (Test-Path -LiteralPath $statusPath -PathType Leaf)) { continue }
            try { $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json -ErrorAction Stop }
            catch { continue }
            $state = [string]$status.state
            if ($state -notin @('applying', 'stopping', 'replacing', 'awaiting_health', 'rollback_failed')) { continue }
            if (-not $status.install_root -or
                -not [System.IO.Path]::GetFullPath([string]$status.install_root).Equals($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
                continue
            }

            $helperIsRunning = Test-UpdateHelperRunning $status $stageDirectory.FullName
            # Validation happens before the runtime is stopped. If that phase
            # is still running, allow the normal existing-instance check below.
            if ($state -eq 'applying' -and $helperIsRunning) { continue }

            $recoveryHelper = Join-Path $stageDirectory.FullName 'Apply-LocalAIRuntimeUpdate.ps1'
            if (-not (Test-Path -LiteralPath $recoveryHelper -PathType Leaf)) {
                $recoveryHelper = Join-Path $root 'scripts/Apply-LocalAIRuntimeUpdate.ps1'
            }
            if (-not (Test-Path -LiteralPath $recoveryHelper -PathType Leaf)) {
                throw "An interrupted update ($($status.tag_name)) needs recovery, but the recovery helper is missing. Preserve '$($status.backup_path)' and restore files manually before starting."
            }

            & $recoveryHelper -InstallRoot $rootFull -DataRoot $dataRoot -StageRoot $stageDirectory.FullName -RecoverInterrupted
            $recovered = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json -ErrorAction Stop
            if ([string]$recovered.state -eq 'rollback_failed') {
                throw "Automatic update recovery failed. Preserve '$($recovered.backup_path)' and inspect '$dataRoot\logs\update-apply.log'. $($recovered.message)"
            }
            if ([string]$recovered.state -eq 'rolled_back') {
                Write-Host "Recovered an interrupted update and restored version $($recovered.previous_version)." -ForegroundColor Yellow
            } elseif ([string]$recovered.state -eq 'failed') {
                Write-Host 'An interrupted update was detected before application files changed. The existing installation was retained.' -ForegroundColor Yellow
            }
        }
    }

    # Release packages place the executable beside this script. Developer
    # builds place it under build/Release (Visual Studio) or build/ (Ninja).
    $candidateExecutables = @(
        (Join-Path $root 'local-ai-runtime.exe'),
        (Join-Path $root 'build/Release/local-ai-runtime.exe'),
        (Join-Path $root 'build/Debug/local-ai-runtime.exe'),
        (Join-Path $root 'build/local-ai-runtime.exe')
    )
    $exe = $candidateExecutables | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $exe) {
        throw "Runtime executable not found. Run .\scripts\build.ps1 first, or extract a release package. Checked: $($candidateExecutables -join '; ')"
    }

    $configDir = Join-Path $dataRoot 'config'
    $configPath = Join-Path $configDir 'engines.json'
    $examplePath = Join-Path $root 'config/engines.example.json'
    $legacyConfigPath = Join-Path $root 'config/engines.json'
    $diagnosticLogPath = Join-Path $dataRoot 'logs/runtime.log'
    New-Item -ItemType Directory -Force -Path $configDir, $dataRoot | Out-Null
    if (-not (Test-Path -LiteralPath $configPath)) {
        if (Test-Path -LiteralPath $legacyConfigPath) {
            Copy-Item $legacyConfigPath $configPath
            Write-Host "Migrated existing settings to $configPath" -ForegroundColor Yellow
        } else {
            if (-not (Test-Path -LiteralPath $examplePath)) {
                throw "Example engine configuration not found: $examplePath"
            }
            Copy-Item $examplePath $configPath
            Write-Host "Created user config: $configPath" -ForegroundColor Yellow
        }
    }

    try {
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json -ErrorAction Stop
        $port = [int]$config.server.port
        if ($port -lt 1 -or $port -gt 65535) { throw "Invalid configured port: $port" }
    } catch {
        throw "Could not read the runtime port from '$configPath'. Check the server.port value. Details: $($_.Exception.Message)"
    }
    $baseUrl = "http://127.0.0.1:$port"

    function Get-RuntimeHealth {
        try {
            # -UseBasicParsing avoids the legacy Internet Explorer parsing prompt
            # in Windows PowerShell 5.1.
            $response = Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/health" -TimeoutSec 2 -ErrorAction Stop
            if ($response.StatusCode -ne 200) { return $null }
            $health = $response.Content | ConvertFrom-Json -ErrorAction Stop
            if ($health.status -eq 'ok' -and $health.version) { return $health }
        } catch { }
        return $null
    }

    function Get-PortListenerIds {
        try {
            return @(
                Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue |
                    Select-Object -ExpandProperty OwningProcess -Unique
            )
        } catch {
            return @()
        }
    }

    function Get-ProcessDetails([int]$ProcessId) {
        # TCP owner lookups can occasionally return PID 0 or the Windows System
        # process for kernel-managed/reserved listeners. These values are not
        # actionable user processes and must never be presented as "System Idle
        # Process" or sent to taskkill. Preserve them as explicit diagnostic data.
        if ($ProcessId -eq 0) {
            return [pscustomobject]@{ Id = 0; Name = 'Windows reported no user-process owner (PID 0)'; ExecutablePath = '' }
        }
        if ($ProcessId -eq 4) {
            return [pscustomobject]@{ Id = 4; Name = 'Windows System (kernel-owned listener)'; ExecutablePath = '' }
        }
        if ($ProcessId -lt 0) {
            return [pscustomobject]@{ Id = $ProcessId; Name = 'Unknown Windows listener owner'; ExecutablePath = '' }
        }
        try {
            $processInfo = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction Stop
            if ($processInfo) {
                return [pscustomobject]@{
                    Id = $ProcessId
                    Name = [string]$processInfo.Name
                    ExecutablePath = [string]$processInfo.ExecutablePath
                }
            }
        } catch { }
        try {
            $processInfo = Get-Process -Id $ProcessId -ErrorAction Stop
            return [pscustomobject]@{ Id = $ProcessId; Name = [string]$processInfo.ProcessName; ExecutablePath = '' }
        } catch { }
        return [pscustomobject]@{ Id = $ProcessId; Name = 'unknown'; ExecutablePath = '' }
    }

    function Wait-ForPortRelease([int]$Attempts = 24) {
        for ($i = 0; $i -lt $Attempts; $i++) {
            if (@(Get-PortListenerIds).Count -eq 0) { return $true }
            Start-Sleep -Milliseconds 250
        }
        return $false
    }

    function Find-AvailableRuntimePort([int]$StartingPort) {
        $enginePorts = @()
        foreach ($engine in @($config.engines)) {
            $engineArgs = @($engine.args)
            for ($index = 0; $index -lt ($engineArgs.Count - 1); $index++) {
                if ([string]$engineArgs[$index] -eq '--port') {
                    $enginePort = 0
                    if ([int]::TryParse([string]$engineArgs[$index + 1], [ref]$enginePort)) {
                        $enginePorts += $enginePort
                    }
                }
            }
        }

        # Keep the familiar 8000-series when the configured port is unavailable.
        # Start just above the configured port when possible, then wrap to 8000.
        # Exclude known engine ports and prove a candidate can bind before saving it.
        $firstCandidate = if ($StartingPort -ge 8000 -and $StartingPort -lt 8999) { $StartingPort + 1 } else { 8000 }
        $candidatePorts = @()
        for ($candidate = $firstCandidate; $candidate -le 8999; $candidate++) { $candidatePorts += $candidate }
        for ($candidate = 8000; $candidate -lt $firstCandidate; $candidate++) { $candidatePorts += $candidate }
        foreach ($candidate in $candidatePorts) {
            if ($enginePorts -contains $candidate) { continue }
            try {
                if (@(Get-NetTCPConnection -LocalPort $candidate -State Listen -ErrorAction SilentlyContinue).Count -gt 0) { continue }
                $probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $candidate)
                try { $probe.Start(); return $candidate }
                finally { try { $probe.Stop() } catch { } }
            } catch {
                # Continue scanning when the candidate is reserved or unavailable.
            }
        }
        return 0
    }

    function Test-PortCanBind([int]$CandidatePort) {
        # Get-NetTCPConnection can report a stale or kernel-owned listener entry.
        # Before moving the user's configured port again, ask Winsock directly
        # whether this process can bind the loopback endpoint. A successful bind
        # probe is stronger evidence that the port is currently available.
        $probe = $null
        try {
            $probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $CandidatePort)
            $probe.Start()
            return $true
        } catch {
            return $false
        } finally {
            if ($probe) { try { $probe.Stop() } catch { } }
        }
    }

    function Stop-ConfirmedRuntimeOwners($Owners) {
        # Ask the old instance to shut down normally first so EngineManager can
        # stop its llama-server child process before the runtime exits.
        if (Get-RuntimeHealth) {
            try {
                $null = Invoke-WebRequest -UseBasicParsing -Uri "$baseUrl/v1/runtime/shutdown" `
                    -Method Post -ContentType 'application/json' -Body '{}' -TimeoutSec 3 -ErrorAction Stop
            } catch { }
            if (Wait-ForPortRelease 24) { return }
        }

        # Only after the user has confirmed the operation, fall back to killing
        # the verified Local AI Runtime process tree. This also avoids leaving
        # its managed inference-engine child alive on the engine port.
        $taskkill = Join-Path $env:WINDIR 'System32/taskkill.exe'
        foreach ($owner in $Owners) {
            $remainingIds = @(Get-PortListenerIds)
            if ($remainingIds -notcontains [int]$owner.Id) { continue }
            $currentOwner = Get-ProcessDetails ([int]$owner.Id)
            if ($currentOwner.Name -ine 'local-ai-runtime.exe' -and $currentOwner.Name -ine 'local-ai-runtime') {
                throw "PID $($owner.Id) no longer belongs to Local AI Runtime. No other process was stopped. Inspect port $port again."
            }
            $taskkillOutput = & $taskkill /PID ([string]$owner.Id) /T /F 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "Could not stop the confirmed Local AI Runtime process tree for PID $($owner.Id). Output: $($taskkillOutput -join ' ')"
            }
        }
        if (-not (Wait-ForPortRelease 24)) {
            throw "The confirmed Local AI Runtime process tree was stopped, but port $port is still occupied by another process. Inspect the owning PID before retrying."
        }
    }

    # Reuse the same healthy installation. If a different extracted copy is
    # still running, ask before a graceful shutdown/restart so preview updates
    # actually use the new package without interrupting engines unexpectedly.
    $listenerIds = @(Get-PortListenerIds)
    if ($listenerIds.Count -gt 0) {
        $listenerDetails = @($listenerIds | ForEach-Object { Get-ProcessDetails ([int]$_) })
        $runtimeOwners = @($listenerDetails | Where-Object { $_.Name -ieq 'local-ai-runtime.exe' -or $_.Name -ieq 'local-ai-runtime' })
        $health = Get-RuntimeHealth

        if ($runtimeOwners.Count -gt 0 -and $runtimeOwners.Count -eq $listenerDetails.Count) {
            $currentExePath = [System.IO.Path]::GetFullPath($exe)
            $sameInstallation = $true
            foreach ($owner in $runtimeOwners) {
                if ([string]::IsNullOrWhiteSpace($owner.ExecutablePath)) { $sameInstallation = $false; break }
                try { $ownerPath = [System.IO.Path]::GetFullPath($owner.ExecutablePath) }
                catch { $sameInstallation = $false; break }
                if (-not [string]::Equals($ownerPath, $currentExePath, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $sameInstallation = $false
                    break
                }
            }

            if ($health -and $sameInstallation) {
                Write-Host "A Local AI Runtime instance from this folder is already healthy on $baseUrl. Opening it instead of starting a duplicate." -ForegroundColor Yellow
                if (-not $NoBrowser) { Start-Process $baseUrl }
                if ($PassThru) { throw 'A runtime instance was already running; no new process handle is available.' }
                return
            }

            $ownersText = ($runtimeOwners | ForEach-Object {
                $pathText = if ([string]::IsNullOrWhiteSpace($_.ExecutablePath)) { 'path unavailable' } else { $_.ExecutablePath }
                "PID $($_.Id) ($pathText)"
            }) -join ', '
            $reason = if ($health) { 'is healthy but runs from a different application folder' } else { 'does not answer the /health check' }
            if ($NoPrompts) {
                throw "Another Local AI Runtime instance on port $port $reason ($ownersText). No process was stopped. Close the other Local AI Runtime normally, then reopen this app."
            }
            $answer = Read-Host "Existing Local AI Runtime on port $port $reason ($ownersText). Stop it and its managed engine process, then start this copy? [y/N]"
            if ($answer -match '^(?i:y|yes)$') {
                Stop-ConfirmedRuntimeOwners $runtimeOwners
            } else {
                throw "The existing Local AI Runtime was left running. Close it normally, then rerun this launcher if you want to use this application folder."
            }
        } else {
            $detailsText = ($listenerDetails | ForEach-Object {
                $location = if ([string]::IsNullOrWhiteSpace($_.ExecutablePath)) { 'path unavailable' } else { $_.ExecutablePath }
                "PID $($_.Id): $($_.Name) ($location)"
            }) -join '; '
            $healthMessage = if ($health) { 'the port responds to /health but the owner is not the Local AI Runtime executable' } else { "$baseUrl/health is not responding" }
            $unresolvedOwner = @($listenerDetails | Where-Object { $_.Id -le 0 -or $_.Id -eq 4 }).Count -gt 0
            if ($unresolvedOwner) {
                # PID 0 and PID 4 are not stoppable application processes. When
                # Windows reports one of these owners without a healthy runtime,
                # choose a free loopback port instead of telling a regular user
                # to edit JSON or risking termination of a system process.
                if (Test-PortCanBind $port) {
                    # The Windows listener table may temporarily report PID 0/4
                    # even though Winsock can bind the configured endpoint. Reuse
                    # the configured port instead of monotonically changing it on
                    # every launch. The runtime itself will perform the final bind.
                    $message = "launcher.port_owner_unresolved_but_bindable port=$port"
                    try { Add-Content -LiteralPath $diagnosticLogPath -Value ("$(Get-Date -Format o) WARN $message") -Encoding UTF8 } catch { }
                    Write-Host "Windows could not resolve the listener owner for port $port, but a direct local bind check succeeded. Reusing the configured port; no other process was stopped." -ForegroundColor Yellow
                } else {
                    $replacementPort = Find-AvailableRuntimePort $port
                    if ($replacementPort -le 0) {
                        throw "Port $port is reported as occupied but Windows did not provide a stoppable application owner ($detailsText); $healthMessage. Nothing was terminated and no free port was found in 1024-8999. Restart Windows or ask support to inspect reserved TCP port ranges."
                    }
                    $oldPort = $port
                    $config.server.port = $replacementPort
                    $temporaryConfig = "$configPath.port-$PID.tmp"
                    try {
                        $serialized = ConvertTo-Json -InputObject $config -Depth 100
                        [System.IO.File]::WriteAllText($temporaryConfig, $serialized + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))
                        Move-Item -LiteralPath $temporaryConfig -Destination $configPath -Force
                    } catch {
                        Remove-Item -LiteralPath $temporaryConfig -Force -ErrorAction SilentlyContinue
                        throw "Windows reported an unresolved owner on port $oldPort, and the runtime could not save a safe replacement port: $($_.Exception.Message). No process was stopped."
                    }
                    $port = $replacementPort
                    $baseUrl = "http://127.0.0.1:$port"
                    $message = "launcher.port_reassigned old_port=$oldPort new_port=$port reason=unresolved_windows_listener_owner"
                    try { Add-Content -LiteralPath $diagnosticLogPath -Value ("$(Get-Date -Format o) WARN $message") -Encoding UTF8 } catch { }
                    Write-Host "Windows did not identify a safe process owner for port $oldPort. Local AI Runtime will use free local port $port instead; no other process was stopped." -ForegroundColor Yellow
                }
            } else {
                throw "Port $port is in use and $healthMessage. Owner: $detailsText. Local AI Runtime will not stop another program. Stop that program or change server.port in '$configPath'."
            }
        }
    }

    # Start-Process flattens ArgumentList to one command line; quote paths so
    # user names or package directories containing spaces remain valid.
    $webDir = Join-Path $root 'web'
    $arguments = '--config "{0}" --web-dir "{1}"' -f $configPath, $webDir
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $dataRoot -WindowStyle Hidden -PassThru
    $ready = $false
    for ($i = 0; $i -lt 40; $i++) {
        if ($process.HasExited) {
            $tail = ''
            if (Test-Path -LiteralPath $diagnosticLogPath) {
                $tail = (Get-Content -LiteralPath $diagnosticLogPath -Tail 12 -ErrorAction SilentlyContinue | Out-String).Trim()
            }
            throw "Runtime exited during startup with code $($process.ExitCode). Startup details are recorded in '$diagnosticLogPath'.`n$tail"
        }
        if (Get-RuntimeHealth) { $ready = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) {
        $tail = ''
        if (Test-Path -LiteralPath $diagnosticLogPath) {
            $tail = (Get-Content -LiteralPath $diagnosticLogPath -Tail 12 -ErrorAction SilentlyContinue | Out-String).Trim()
        }
        throw "Runtime did not become ready on $baseUrl within 10 seconds. Check '$diagnosticLogPath'.`n$tail"
    }
    if (-not $NoBrowser) { Start-Process $baseUrl }
    Write-Host "Local AI Runtime is running from '$exe'." -ForegroundColor Green
    Write-Host "Dashboard: $baseUrl"
    Write-Host "User settings, engine/model files and logs: '$dataRoot'"
    if ($PassThru) { return $process }
} finally {
    Pop-Location
}
