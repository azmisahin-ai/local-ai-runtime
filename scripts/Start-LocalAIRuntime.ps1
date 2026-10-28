[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    $exe = Join-Path $root 'local-ai-runtime.exe'
    if (-not (Test-Path $exe)) { throw "Runtime executable not found: $exe" }

    $configDir = Join-Path $root 'config'
    $configPath = Join-Path $configDir 'engines.json'
    $examplePath = Join-Path $configDir 'engines.example.json'
    if (-not (Test-Path $configPath)) {
        Copy-Item $examplePath $configPath
        Write-Host "Created $configPath. Edit its engine executable/model paths to enable inference." -ForegroundColor Yellow
    }

    $existing = Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 8000 -State Listen -ErrorAction SilentlyContinue
    if ($existing) {
        try {
            $health = Invoke-RestMethod -Uri 'http://127.0.0.1:8000/health' -TimeoutSec 2
            if ($health.status -eq 'ok' -and $health.version) {
                Write-Host 'A Local AI Runtime instance is already listening. Opening its dashboard.' -ForegroundColor Yellow
                Start-Process 'http://127.0.0.1:8000'
                return
            }
        } catch { }
        throw 'Port 8000 is already in use by another service. Stop that service or change server.port in config/engines.json.'
    }

    $process = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru
    $ready = $false
    for ($i = 0; $i -lt 40; $i++) {
        if ($process.HasExited) { throw "Runtime exited early with code $($process.ExitCode). Run local-ai-runtime.exe from PowerShell to inspect its output." }
        try {
            $null = Invoke-WebRequest -Uri 'http://127.0.0.1:8000/health' -TimeoutSec 1
            $ready = $true
            break
        } catch { Start-Sleep -Milliseconds 250 }
    }
    if (-not $ready) { throw 'Runtime did not become ready on http://127.0.0.1:8000 within 10 seconds.' }
    Start-Process 'http://127.0.0.1:8000'
    Write-Host 'Local AI Runtime is running. Close its process window or stop the process to shut it down.' -ForegroundColor Green
} finally {
    Pop-Location
}
