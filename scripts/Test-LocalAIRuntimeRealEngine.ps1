[CmdletBinding()]
param(
    [string]$RuntimeRoot = '',
    [string]$DataRoot = '',
    [ValidateRange(10, 1800)][int]$ReadyTimeoutSeconds = 300,
    [ValidateRange(10, 120)][int]$ChatTimeoutSeconds = 120,
    [string]$Prompt = 'Reply with exactly: LOCAL_AI_RUNTIME_OK',
    [string]$ReportPath = '',
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$defaultRuntimeRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
    $root = $defaultRuntimeRoot
} else {
    $root = [System.IO.Path]::GetFullPath($RuntimeRoot)
}
$launcher = Join-Path $root 'scripts/Start-LocalAIRuntime.ps1'
if ([string]::IsNullOrWhiteSpace($DataRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $DataRoot = Join-Path $env:LOCALAPPDATA 'LocalAIRuntime'
    } else {
        $DataRoot = Join-Path $env:TEMP 'LocalAIRuntime'
    }
}
$dataRootFull = [System.IO.Path]::GetFullPath($DataRoot).TrimEnd('\')
$configPath = Join-Path $dataRootFull 'config/engines.json'
$reportLines = New-Object 'System.Collections.Generic.List[string]'
$runtimeBaseUrl = ''
$runtimeHealth = $null
$runtimeStartedByTest = $false
$startedRuntime = $null
$engineId = ''
$engineStartedByTest = $false
$engineWasRunning = $false
$keepRunningText = 'No'
$failureMessage = ''
$engineReadySeconds = $null
$chatSeconds = $null
$modelsCount = 0
$responseCharacters = 0
$engineFileName = ''
$modelFileName = ''
$backendLabel = 'unknown'
$testResult = 'FAIL'
$cleanupNotes = New-Object 'System.Collections.Generic.List[string]'

function Get-ConfigObject {
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { return $null }
    try {
        return (Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json -ErrorAction Stop)
    } catch {
        throw "Could not parse user configuration '$configPath': $($_.Exception.Message)"
    }
}

function Get-BaseUrlFromConfig($Config) {
    if (-not $Config -or -not $Config.server -or $null -eq $Config.server.port) {
        throw "The runtime config does not contain server.port: $configPath"
    }
    $port = 0
    if (-not [int]::TryParse([string]$Config.server.port, [ref]$port) -or $port -lt 1 -or $port -gt 65535) {
        throw "The runtime config has an invalid server.port: $($Config.server.port)"
    }
    return "http://127.0.0.1:$port"
}

function Get-HealthyRuntime([string]$BaseUrl) {
    try {
        $health = Invoke-RestMethod -UseBasicParsing -Uri "$BaseUrl/health" -TimeoutSec 3 -ErrorAction Stop
        if ($health.status -eq 'ok' -and -not [string]::IsNullOrWhiteSpace([string]$health.version)) {
            return $health
        }
    } catch { }
    return $null
}

function Invoke-LocalApi {
    param(
        [Parameter(Mandatory = $true)][string]$BaseUrl,
        [Parameter(Mandatory = $true)][string]$Path,
        [string]$Method = 'GET',
        [object]$Body = $null,
        [ValidateRange(1, 600)][int]$TimeoutSec = 10
    )
    $uri = $BaseUrl.TrimEnd('/') + $Path
    try {
        if ($Method -eq 'GET') {
            return Invoke-RestMethod -UseBasicParsing -Uri $uri -TimeoutSec $TimeoutSec -ErrorAction Stop
        }
        $jsonBody = if ($null -eq $Body) { '{}' } else { ConvertTo-Json -InputObject $Body -Depth 8 -Compress }
        return Invoke-RestMethod -UseBasicParsing -Uri $uri -Method $Method -ContentType 'application/json' -Body $jsonBody -TimeoutSec $TimeoutSec -ErrorAction Stop
    } catch {
        $details = $_.Exception.Message
        if ($_.ErrorDetails -and -not [string]::IsNullOrWhiteSpace($_.ErrorDetails.Message)) {
            $details += ' Response: ' + $_.ErrorDetails.Message
        }
        throw "Local API request $Method $Path failed: $details"
    }
}

function Get-EngineStatus($EnginesResponse, [string]$Id) {
    if (-not $EnginesResponse -or $null -eq $EnginesResponse.data) { return $null }
    $engineMatches = @($EnginesResponse.data | Where-Object { [string]$_.id -eq $Id } | Select-Object -First 1)
    if ($engineMatches.Count -eq 0) { return $null }
    return $engineMatches[0]
}

function Normalize-ConfiguredPath([string]$Value) {
    if ($null -eq $Value) { return '' }
    $normalized = $Value.Trim()
    if ($normalized.Length -ge 2 -and $normalized.StartsWith('"') -and $normalized.EndsWith('"')) {
        $normalized = $normalized.Substring(1, $normalized.Length - 2)
    }
    return $normalized
}

function Stop-TestStartedEngine {
    if (-not $engineStartedByTest -or $KeepRunning -or [string]::IsNullOrWhiteSpace($runtimeBaseUrl) -or [string]::IsNullOrWhiteSpace($engineId)) { return }
    try {
        $escapedId = [System.Uri]::EscapeDataString($engineId)
        $null = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path "/v1/engines/$escapedId/stop" -Method 'POST' -TimeoutSec 15
        $cleanupNotes.Add('Engine started by the test was stopped.')
    } catch {
        $cleanupNotes.Add("Engine cleanup failed: $($_.Exception.Message)")
        if ([string]::IsNullOrWhiteSpace($failureMessage)) { $script:failureMessage = "The inference test finished, but cleanup failed: $($_.Exception.Message)" }
    }
}

function Stop-TestStartedRuntime {
    if (-not $runtimeStartedByTest -or $KeepRunning -or [string]::IsNullOrWhiteSpace($runtimeBaseUrl)) { return }
    try {
        try { $null = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path '/v1/runtime/shutdown' -Method 'POST' -TimeoutSec 3 } catch { }
        if ($startedRuntime -and $startedRuntime.Id) {
            try { Wait-Process -Id $startedRuntime.Id -Timeout 15 -ErrorAction Stop } catch {
                try { $startedRuntime.Refresh() } catch { }
                if ($startedRuntime -and -not $startedRuntime.HasExited) {
                    $cleanupNotes.Add("Runtime PID $($startedRuntime.Id) did not exit after graceful shutdown; it was left running rather than force-killed.")
                    if ([string]::IsNullOrWhiteSpace($failureMessage)) { $script:failureMessage = 'The test-started runtime did not exit after a graceful shutdown. Check the runtime log before stopping it manually.' }
                    return
                }
            }
        }
        $cleanupNotes.Add('Runtime started by the test was shut down.')
    } catch {
        $cleanupNotes.Add("Runtime cleanup warning: $($_.Exception.Message)")
        if ([string]::IsNullOrWhiteSpace($failureMessage)) { $script:failureMessage = "The test-started runtime could not be shut down cleanly: $($_.Exception.Message)" }
    }
}

try {
    $reportLines.Add('Local AI Runtime — real engine smoke test')
    $reportLines.Add("Started: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
    $reportLines.Add("Application folder: $root")
    $reportLines.Add("User data root: $dataRootFull")

    if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) { throw "Runtime launcher not found: $launcher. Run this script from an extracted release package or repository folder." }

    $config = Get-ConfigObject
    if ($config) {
        $runtimeBaseUrl = Get-BaseUrlFromConfig $config
        $runtimeHealth = Get-HealthyRuntime $runtimeBaseUrl
    }

    if (-not $runtimeHealth) {
        Write-Host 'Runtime is not healthy yet; starting it with the standard Windows launcher...' -ForegroundColor Yellow
        $startedRuntime = & $launcher -NoBrowser -PassThru -DataRoot $dataRootFull
        if (-not $startedRuntime -or -not $startedRuntime.Id) {
            throw 'The launcher did not return a runtime process. See its startup message and the local runtime log.'
        }
        $runtimeStartedByTest = $true
        $config = Get-ConfigObject
        $runtimeBaseUrl = Get-BaseUrlFromConfig $config
        $runtimeHealth = Get-HealthyRuntime $runtimeBaseUrl
        if (-not $runtimeHealth) { throw "Runtime did not answer /health at $runtimeBaseUrl after launch." }
    }

    if (-not $config) { $config = Get-ConfigObject }
    if (-not $config) { throw "User config was not created: $configPath" }
    if (-not $runtimeHealth) { $runtimeHealth = Get-HealthyRuntime $runtimeBaseUrl }
    if (-not $runtimeHealth) { throw "Runtime health check failed at $runtimeBaseUrl." }

    $reportLines.Add("Runtime version: $($runtimeHealth.version)")
    $reportLines.Add("Runtime health endpoint: PASS ($runtimeBaseUrl/health)")

    $configuredEngines = @($config.engines)
    $defaultEngines = @($configuredEngines | Where-Object { $_.default_for_chat -eq $true })
    if ($defaultEngines.Count -ne 1) {
        throw "Expected exactly one default chat engine in '$configPath'; found $($defaultEngines.Count). Open the dashboard and configure llama.cpp as the default chat engine."
    }
    $selectedEngine = $defaultEngines[0]
    $engineId = [string]$selectedEngine.id
    if ([string]::IsNullOrWhiteSpace($engineId)) { throw 'The default chat engine has no id.' }
    $engineFile = Normalize-ConfiguredPath ([string]$selectedEngine.executable_path)
    $modelFile = Normalize-ConfiguredPath ([string]$selectedEngine.model_path)
    if ([string]::IsNullOrWhiteSpace($engineFile) -or -not (Test-Path -LiteralPath $engineFile -PathType Leaf)) {
        throw "The configured default engine executable is missing: '$engineFile'. Install the llama.cpp component or fix its path in the dashboard."
    }
    if ([System.IO.Path]::GetFileName($engineFile) -notmatch '^(?i:llama-server)(\.exe)?$') {
        throw "The configured default engine does not look like llama-server: '$([System.IO.Path]::GetFileName($engineFile))'. Configure llama-server.exe as the default chat engine before running this real-engine test."
    }
    if ([string]::IsNullOrWhiteSpace($modelFile) -or -not (Test-Path -LiteralPath $modelFile -PathType Leaf)) {
        throw "The configured model file is missing: '$modelFile'. Install the starter GGUF model or correct its path in the dashboard."
    }
    if ([System.IO.Path]::GetExtension($modelFile) -ine '.gguf') { throw "The configured model is not a .gguf file: '$modelFile'" }
    $modelStream = [System.IO.File]::OpenRead($modelFile)
    try {
        $magicBytes = New-Object byte[] 4
        $readCount = $modelStream.Read($magicBytes, 0, 4)
        $magic = if ($readCount -eq 4) { [System.Text.Encoding]::ASCII.GetString($magicBytes) } else { '' }
    } finally { $modelStream.Dispose() }
    if ($magic -cne 'GGUF') { throw "The configured model does not have a GGUF header: '$modelFile'. Reinstall or verify the model before retrying." }

    $engineFileName = [System.IO.Path]::GetFileName($engineFile)
    $modelFileName = [System.IO.Path]::GetFileName($modelFile)
    $argList = @($selectedEngine.args | ForEach-Object { [string]$_ })
    for ($argIndex = 0; $argIndex -lt $argList.Count; $argIndex++) {
        if ($argList[$argIndex] -eq '--n-gpu-layers' -and ($argIndex + 1) -lt $argList.Count) {
            if ($argList[$argIndex + 1] -eq '0') { $backendLabel = 'CPU (n-gpu-layers=0)' }
            elseif ($argList[$argIndex + 1] -eq 'auto') { $backendLabel = 'GPU auto-placement (requested; not proof of CUDA use)' }
            else { $backendLabel = "n-gpu-layers=$($argList[$argIndex + 1])" }
            break
        }
    }

    $reportLines.Add("Default engine id: $engineId")
    $reportLines.Add("Engine binary: $engineFileName")
    $reportLines.Add("Model file: $modelFileName")
    $reportLines.Add("Configured backend: $backendLabel")
    $reportLines.Add('GGUF header: PASS')
    Write-Host "Testing engine '$engineId' with model '$modelFileName'..." -ForegroundColor Cyan

    $enginesResponse = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path '/v1/engines' -TimeoutSec 10
    $engineStatus = Get-EngineStatus $enginesResponse $engineId
    if (-not $engineStatus) { throw "Runtime API did not report configured engine '$engineId' at /v1/engines." }
    $engineWasRunning = ([string]$engineStatus.status -eq 'running')
    if (-not $engineWasRunning) {
        $escapedEngineId = [System.Uri]::EscapeDataString($engineId)
        $null = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path "/v1/engines/$escapedEngineId/start" -Method 'POST' -TimeoutSec 20
        $engineStartedByTest = $true
    }

    $readyClock = [System.Diagnostics.Stopwatch]::StartNew()
    $readyStatus = $null
    while ($readyClock.Elapsed.TotalSeconds -lt $ReadyTimeoutSeconds) {
        $enginesResponse = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path '/v1/engines' -TimeoutSec 10
        $readyStatus = Get-EngineStatus $enginesResponse $engineId
        if ($readyStatus -and $readyStatus.ready -eq $true) { break }
        if ($readyStatus -and [string]$readyStatus.status -eq 'failed') {
            throw "Engine process failed while loading the model: $($readyStatus.last_error)"
        }
        Start-Sleep -Seconds 2
    }
    $readyClock.Stop()
    if (-not $readyStatus -or $readyStatus.ready -ne $true) {
        $lastError = if ($readyStatus) { [string]$readyStatus.last_error } else { 'No engine status was returned.' }
        throw "Engine did not become ready within $ReadyTimeoutSeconds seconds. Current state='$($readyStatus.readiness)'; last_error='$lastError'. See engine/runtime logs under '$dataRootFull\logs'."
    }
    $engineReadySeconds = [math]::Round($readyClock.Elapsed.TotalSeconds, 1)
    $reportLines.Add("Engine readiness: PASS ($engineReadySeconds seconds; PID $($readyStatus.pid))")
    Write-Host "Engine ready in $engineReadySeconds seconds." -ForegroundColor Green

    $modelsResponse = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path '/v1/models' -TimeoutSec 30
    $modelsCount = @($modelsResponse.data).Count
    if ($modelsCount -lt 1) { throw 'The configured engine returned no entries from /v1/models.' }
    $reportLines.Add("OpenAI-compatible /v1/models: PASS ($modelsCount model entry/entries)")

    $chatBody = @{
        model = 'local-model'
        messages = @(
            @{ role = 'system'; content = 'You are running a local runtime smoke test. Follow the user instruction exactly and answer briefly.' },
            @{ role = 'user'; content = $Prompt }
        )
        temperature = 0
        max_tokens = 48
        stream = $false
    }
    $chatClock = [System.Diagnostics.Stopwatch]::StartNew()
    $chatResponse = Invoke-LocalApi -BaseUrl $runtimeBaseUrl -Path '/v1/chat/completions' -Method 'POST' -Body $chatBody -TimeoutSec $ChatTimeoutSeconds
    $chatClock.Stop()
    $chatSeconds = [math]::Round($chatClock.Elapsed.TotalSeconds, 1)
    $generatedText = ''
    $chatChoices = @($chatResponse.choices)
    if ($chatChoices.Count -gt 0) {
        if ($chatChoices[0].message -and $null -ne $chatChoices[0].message.content) {
            $generatedText = [string]$chatChoices[0].message.content
        } elseif ($null -ne $chatChoices[0].text) {
            $generatedText = [string]$chatChoices[0].text
        }
    }
    if ([string]::IsNullOrWhiteSpace($generatedText)) {
        throw 'The chat completion endpoint returned no text in choices[0].message.content. Inspect the engine log for the model or template error.'
    }
    $responseCharacters = $generatedText.Length
    $reportLines.Add("Chat completion: PASS ($chatSeconds seconds; $responseCharacters response characters)")
    $reportLines.Add('Generated response text omitted from this report.')
    $testResult = 'PASS'
    Write-Host 'Real local inference succeeded.' -ForegroundColor Green
    Write-Host "Chat response length: $responseCharacters characters; elapsed: $chatSeconds seconds."
} catch {
    $failureMessage = $_.Exception.Message
    $reportLines.Add("Failure: $failureMessage")
} finally {
    if ($engineWasRunning -and -not $engineStartedByTest) {
        $cleanupNotes.Add('Engine was already running before the test and was left running.')
    }
    if ($KeepRunning) {
        $keepRunningText = 'Yes'
        $cleanupNotes.Add('KeepRunning was selected; the test did not stop its engine/runtime.')
    } else {
        Stop-TestStartedEngine
        Stop-TestStartedRuntime
    }

    if ($cleanupNotes.Count -gt 0) {
        $reportLines.Add('Cleanup:')
        foreach ($note in $cleanupNotes) { $reportLines.Add("- $note") }
    }
    if (-not [string]::IsNullOrWhiteSpace($failureMessage)) { $testResult = 'FAIL' }
    $reportLines.Insert(2, "Result: $testResult")
    $reportLines.Add("Keep running: $keepRunningText")
    if ($engineReadySeconds -ne $null) { $reportLines.Add("Engine readiness seconds: $engineReadySeconds") }
    if ($chatSeconds -ne $null) { $reportLines.Add("Chat completion seconds: $chatSeconds") }
    if ($modelsCount -gt 0) { $reportLines.Add("Models listed: $modelsCount") }
    if ($responseCharacters -gt 0) { $reportLines.Add("Response character count: $responseCharacters") }
    if ([string]::IsNullOrWhiteSpace($ReportPath)) {
        $logsDir = Join-Path $dataRootFull 'logs'
        $null = New-Item -ItemType Directory -Force -Path $logsDir
        $ReportPath = Join-Path $logsDir ('real-engine-smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
    } else {
        $ReportPath = [System.IO.Path]::GetFullPath($ReportPath)
        $reportParent = Split-Path -Parent $ReportPath
        if (-not [string]::IsNullOrWhiteSpace($reportParent)) { $null = New-Item -ItemType Directory -Force -Path $reportParent }
    }
    try {
        [System.IO.File]::WriteAllLines($ReportPath, $reportLines.ToArray(), [System.Text.UTF8Encoding]::new($false))
        Write-Host "Smoke report: $ReportPath"
    } catch {
        Write-Warning "Could not write the smoke report: $($_.Exception.Message)"
    }
}

if (-not [string]::IsNullOrWhiteSpace($failureMessage)) {
    throw "Local AI Runtime real-engine smoke test FAILED: $failureMessage`nReport: $ReportPath"
}
Write-Host 'Local AI Runtime real-engine smoke test PASSED.' -ForegroundColor Green
