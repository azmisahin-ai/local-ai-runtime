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

    $existing = Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 8000 -State Listen -ErrorAction SilentlyContinue
    if ($existing) {
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:8000/health' -TimeoutSec 2
            if ($health.status -eq 'ok' -and $health.version) {
                Write-Host 'A Local AI Runtime instance is already listening.' -ForegroundColor Yellow
                if (-not $NoBrowser) { Start-Process 'http://127.0.0.1:8000' }
                if ($PassThru) { throw 'A runtime instance was already running; no new process handle is available.' }
                return
            }
        } catch { }
        throw "Port 8000 is already in use by another service. Stop that service or change server.port in $configPath."
    }

    # Start-Process flattens ArgumentList to one command line; quote paths so
    # user names or package directories containing spaces remain valid.
    $webDir = Join-Path $root 'web'
    $arguments = '--config "{0}" --web-dir "{1}"' -f $configPath, $webDir
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $dataRoot -PassThru
    $ready = $false
    for ($i = 0; $i -lt 40; $i++) {
        if ($process.HasExited) {
            throw "Runtime exited early with code $($process.ExitCode). Run '$exe' from PowerShell to inspect startup output."
        }
        try {
            $null = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:8000/health' -TimeoutSec 1
            $ready = $true
            break
        } catch { Start-Sleep -Milliseconds 250 }
    }
    if (-not $ready) { throw 'Runtime did not become ready on http://127.0.0.1:8000 within 10 seconds.' }
    if (-not $NoBrowser) { Start-Process 'http://127.0.0.1:8000' }
    Write-Host "Local AI Runtime is running from '$exe'." -ForegroundColor Green
    Write-Host "User settings, engine/model files and logs: '$dataRoot'"
    if ($PassThru) { return $process }
} finally {
    Pop-Location
}
