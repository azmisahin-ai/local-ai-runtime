[CmdletBinding()]
param(
    [switch]$NoBrowser,
    [switch]$PassThru
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
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

    # Keep config, model/engine downloads and logs outside the program directory
    # so upgrades work even when the runtime folder is read-only (e.g. Program Files).
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $dataRoot = Join-Path $env:LOCALAPPDATA 'LocalAIRuntime'
    } else {
        $dataRoot = Join-Path $env:TEMP 'LocalAIRuntime'
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
            $answer = Read-Host "Existing Local AI Runtime on port $port $reason ($ownersText). Stop it and its managed engine process, then start this copy? [y/N]"
            if ($answer -match '^(?i:y|yes)$') {
                Stop-ConfirmedRuntimeOwners $runtimeOwners
            } else {
                throw "The existing Local AI Runtime was left running. Close its console with Ctrl+C, then rerun this launcher if you want to use this application folder."
            }
        } else {
            $detailsText = ($listenerDetails | ForEach-Object {
                $location = if ([string]::IsNullOrWhiteSpace($_.ExecutablePath)) { 'path unavailable' } else { $_.ExecutablePath }
                "PID $($_.Id): $($_.Name) ($location)"
            }) -join '; '
            $healthMessage = if ($health) { 'the port responds to /health but the owner is not the Local AI Runtime executable' } else { "$baseUrl/health is not responding" }
            throw "Port $port is in use and $healthMessage. Owner: $detailsText. Local AI Runtime will not stop another program. Stop that program or change server.port in '$configPath'."
        }
    }

    # Start-Process flattens ArgumentList to one command line; quote paths so
    # user names or package directories containing spaces remain valid.
    $webDir = Join-Path $root 'web'
    $arguments = '--config "{0}" --web-dir "{1}"' -f $configPath, $webDir
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $dataRoot -PassThru
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
