[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$tagName = "v$Version"
$assetName = 'local-ai-runtime-windows-x64.zip'
$testRoot = Join-Path $env:RUNNER_TEMP ('lar-update-apply-smoke-' + [Guid]::NewGuid().ToString('N'))
$installRoot = Join-Path $testRoot 'install'
$dataRoot = Join-Path $testRoot 'user-data'
$stageRoot = Join-Path $dataRoot ('updates\' + $tagName)
$packageCopy = Join-Path $stageRoot $assetName
$manifestPath = Join-Path $stageRoot 'staged-update.json'
$helperPath = Join-Path $installRoot 'scripts\Apply-LocalAIRuntimeUpdate.ps1'
$installedHelperPath = $helperPath
$port = 0
$startedRuntime = $null
$success = $false

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Wait-TestHealth([int]$ExpectedPort, [string]$ExpectedVersion, [int]$Seconds) {
    for ($i = 0; $i -lt ($Seconds * 4); $i++) {
        try {
            $health = Invoke-RestMethod -UseBasicParsing -Uri "http://127.0.0.1:$ExpectedPort/health" -TimeoutSec 1
            if ($health.status -eq 'ok' -and $health.version -eq $ExpectedVersion) { return $true }
        } catch { }
        Start-Sleep -Milliseconds 250
    }
    return $false
}


function Get-SmokeRuntimePid {
    $expectedExe = [System.IO.Path]::GetFullPath((Join-Path $installRoot 'local-ai-runtime.exe'))
    foreach ($listener in @(Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)) {
        try {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction Stop
            if ($proc -and $proc.ExecutablePath -and
                [System.IO.Path]::GetFullPath($proc.ExecutablePath).Equals($expectedExe, [StringComparison]::OrdinalIgnoreCase)) {
                return [int]$proc.ProcessId
            }
        } catch { }
    }
    throw "Could not identify the isolated smoke-test runtime listening on port $port."
}

function Stop-SmokeRuntime {
    if (-not $port) { return }
    $ownedListenerIds = @()
    $listeners = @(Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)
    foreach ($listener in $listeners) {
        try {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction Stop
            if ($proc -and $proc.ExecutablePath -and
                [System.IO.Path]::GetFullPath($proc.ExecutablePath).StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) {
                $ownedListenerIds += [int]$listener.OwningProcess
            }
        } catch { }
    }
    # Never send the test shutdown request to a process that does not belong
    # to this isolated smoke-test directory.
    if ($ownedListenerIds.Count -eq 0) { return }
    try {
        $null = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/v1/runtime/shutdown" -Method Post -ContentType 'application/json' -Body '{}' -TimeoutSec 2 -ErrorAction Stop
    } catch { }
    for ($i = 0; $i -lt 80; $i++) {
        $listeners = @(Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue)
        if ($listeners.Count -eq 0) { return }
        foreach ($listener in $listeners) {
            try {
                $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)" -ErrorAction Stop
                if ($proc -and $proc.ExecutablePath -and
                    [System.IO.Path]::GetFullPath($proc.ExecutablePath).StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)) {
                    Stop-Process -Id ([int]$listener.OwningProcess) -Force -ErrorAction SilentlyContinue
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 250
    }
}

try {
    if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) { throw "Package not found: $PackagePath" }
    $versionOutput = (& (Join-Path $env:GITHUB_WORKSPACE 'build/Release/local-ai-runtime.exe') --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $versionOutput -cne $Version) { throw "Build executable version '$versionOutput' did not match '$Version'." }

    New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
    Expand-Archive -LiteralPath $PackagePath -DestinationPath $installRoot -Force
    if (-not (Test-Path -LiteralPath $installedHelperPath -PathType Leaf)) { throw 'The package did not contain Apply-LocalAIRuntimeUpdate.ps1.' }
    # Mark a pre-existing application file so the successful apply must replace
    # actual application content, not merely re-copy byte-identical test files.
    $webIndexPath = Join-Path $installRoot 'web/index.html'
    $previousWebContent = [System.IO.File]::ReadAllText($webIndexPath)
    Write-Utf8NoBom $webIndexPath ($previousWebContent + "`n<!-- update-smoke-before-apply -->`n")
    $webIndexHashBeforeApply = (Get-FileHash -LiteralPath $webIndexPath -Algorithm SHA256).Hash
    $tokens = $null
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($installedHelperPath, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count -gt 0) { throw "The packaged update helper failed parsing: $($parseErrors -join '; ')" }

    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = [int]$listener.LocalEndpoint.Port
    $listener.Stop()

    $exampleConfigPath = Join-Path $installRoot 'config/engines.example.json'
    $config = Get-Content -LiteralPath $exampleConfigPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $config.server.port = $port
    $userConfigPath = Join-Path $dataRoot 'config/engines.json'
    Write-Utf8NoBom $userConfigPath ($config | ConvertTo-Json -Depth 20)
    $preservedDataPath = Join-Path $dataRoot 'models/preserve-update-test.txt'
    Write-Utf8NoBom $preservedDataPath 'user-data-must-survive-update'
    $configHashBefore = (Get-FileHash -LiteralPath $userConfigPath -Algorithm SHA256).Hash

    New-Item -ItemType Directory -Force -Path $stageRoot, (Join-Path $dataRoot 'logs') | Out-Null
    # Mirror production: run the helper copied into the per-user stage directory,
    # not the copy that belongs to the installation being replaced.
    $helperPath = Join-Path $stageRoot 'Apply-LocalAIRuntimeUpdate.ps1'
    Copy-Item -LiteralPath $installedHelperPath -Destination $helperPath -Force
    Copy-Item -LiteralPath $PackagePath -Destination $packageCopy -Force
    $packageHash = (Get-FileHash -LiteralPath $packageCopy -Algorithm SHA256).Hash.ToLowerInvariant()
    $packageSize = (Get-Item -LiteralPath $packageCopy).Length
    $manifest = [ordered]@{
        tag_name = $tagName
        asset_name = $assetName
        sha256 = $packageHash
        size_bytes = $packageSize
        verified = $true
        apply_supported = $true
    }
    Write-Utf8NoBom $manifestPath ($manifest | ConvertTo-Json -Depth 4)

    $statusPath = Join-Path $stageRoot 'apply-status.json'
    $installedExe = Join-Path $installRoot 'local-ai-runtime.exe'
    $installedExeHashBefore = (Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash

    # Pass 1: corrupt a package copy and prove that digest mismatch fails closed
    # before any application files or user data are changed.
    $tamperedPackage = Join-Path $stageRoot 'tampered-update.zip'
    Copy-Item -LiteralPath $packageCopy -Destination $tamperedPackage -Force
    $tamperStream = [System.IO.File]::Open($tamperedPackage, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    try {
        $tamperStream.Position = 100
        $originalByte = $tamperStream.ReadByte()
        if ($originalByte -lt 0) { throw 'Test archive is too small for tamper-rejection testing.' }
        $tamperStream.Position = 100
        $tamperStream.WriteByte([byte]($originalByte -bxor 1))
    } finally { $tamperStream.Dispose() }
    & $helperPath -InstallRoot $installRoot -DataRoot $dataRoot -StageRoot $stageRoot `
        -PackagePath $tamperedPackage -ManifestPath $manifestPath -TagName $tagName `
        -AssetName $assetName -ExpectedSha256 $packageHash -RuntimePid 0 -Port $port `
        -CurrentVersion $Version -ValidateOnly
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($status.state -ne 'failed' -or $status.message -notmatch 'SHA-256') { throw "A tampered archive was not rejected by its digest: state=$($status.state) message=$($status.message)" }
    if ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash -cne $installedExeHashBefore) { throw 'Digest mismatch unexpectedly changed the installed executable.' }
    if ((Get-FileHash -LiteralPath $userConfigPath -Algorithm SHA256).Hash -cne $configHashBefore) { throw 'Digest mismatch unexpectedly changed user configuration.' }
    Remove-Item -LiteralPath $tamperedPackage -Force

    # Pass 2: validate the exact staged release package without touching install files.
    & $helperPath -InstallRoot $installRoot -DataRoot $dataRoot -StageRoot $stageRoot `
        -PackagePath $packageCopy -ManifestPath $manifestPath -TagName $tagName `
        -AssetName $assetName -ExpectedSha256 $packageHash -RuntimePid 0 -Port $port `
        -CurrentVersion $Version -ValidateOnly
    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($status.state -ne 'validated') { throw "Validate-only phase did not succeed: $($status.message)" }
    $configHashAfterValidate = (Get-FileHash -LiteralPath $userConfigPath -Algorithm SHA256).Hash
    if ($configHashAfterValidate -cne $configHashBefore) { throw 'Validate-only unexpectedly changed user configuration.' }

    # Pass 3: run a temporary runtime, apply the package through the real helper,
    # and verify that the helper stops/restarts the process while preserving data.
    $runtimeExe = Join-Path $installRoot 'local-ai-runtime.exe'
    $webPath = Join-Path $installRoot 'web'
    $stdout = Join-Path $dataRoot 'logs/smoke-before.stdout.log'
    $stderr = Join-Path $dataRoot 'logs/smoke-before.stderr.log'
    $arguments = '--config "{0}" --web-dir "{1}"' -f $userConfigPath, $webPath
    $startedRuntime = Start-Process -FilePath $runtimeExe -ArgumentList $arguments -WorkingDirectory $dataRoot `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    if (-not (Wait-TestHealth $port $Version 12)) { throw 'The temporary pre-update runtime did not become healthy.' }

    & $helperPath -InstallRoot $installRoot -DataRoot $dataRoot -StageRoot $stageRoot `
        -PackagePath $packageCopy -ManifestPath $manifestPath -TagName $tagName `
        -AssetName $assetName -ExpectedSha256 $packageHash -RuntimePid $startedRuntime.Id -Port $port `
        -CurrentVersion $Version

    $status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($status.state -ne 'succeeded') { throw "Apply phase did not succeed: state=$($status.state) message=$($status.message)" }
    if (-not (Wait-TestHealth $port $Version 15)) { throw 'The post-update runtime did not pass /health.' }
    if ((Get-FileHash -LiteralPath $webIndexPath -Algorithm SHA256).Hash -ceq $webIndexHashBeforeApply) { throw 'Apply did not replace the pre-existing dashboard file.' }
    if ([System.IO.File]::ReadAllText($webIndexPath).Contains('update-smoke-before-apply')) { throw 'The successful update left pre-update application content in place.' }
    if ((Get-Content -LiteralPath $preservedDataPath -Raw) -cne 'user-data-must-survive-update') { throw 'The user-data sentinel was modified or removed by update.' }
    if ((Get-FileHash -LiteralPath $userConfigPath -Algorithm SHA256).Hash -cne $configHashBefore) { throw 'The update changed user configuration.' }
    if (-not $status.backup_path -or -not (Test-Path -LiteralPath (Join-Path $status.backup_path 'rollback-manifest.json') -PathType Leaf)) {
        throw 'The successful update did not retain its rollback backup.'
    }

    # Pass 4: make a distinct pre-update file change, force a controlled
    # post-replacement failure, and verify that rollback restores that exact file
    # content and restarts the prior runtime healthy.
    $preRollbackContent = [System.IO.File]::ReadAllText($webIndexPath)
    Write-Utf8NoBom $webIndexPath ($preRollbackContent + "`n<!-- rollback-preservation-sentinel -->`n")
    $webIndexHashBeforeRollback = (Get-FileHash -LiteralPath $webIndexPath -Algorithm SHA256).Hash
    $runtimePidForRollback = Get-SmokeRuntimePid
    & $helperPath -InstallRoot $installRoot -DataRoot $dataRoot -StageRoot $stageRoot `
        -PackagePath $packageCopy -ManifestPath $manifestPath -TagName $tagName `
        -AssetName $assetName -ExpectedSha256 $packageHash -RuntimePid $runtimePidForRollback -Port $port `
        -CurrentVersion $Version -TestForceFailureAfterReplace
    $rollbackStatus = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ($rollbackStatus.state -ne 'rolled_back') { throw "Forced failure did not complete automatic rollback: state=$($rollbackStatus.state) message=$($rollbackStatus.message)" }
    if (-not (Wait-TestHealth $port $Version 15)) { throw 'The previous runtime did not become healthy after rollback.' }
    if ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash -cne $installedExeHashBefore) { throw 'Rollback did not restore the original executable bytes.' }
    if ((Get-FileHash -LiteralPath $webIndexPath -Algorithm SHA256).Hash -cne $webIndexHashBeforeRollback) { throw 'Rollback did not restore the pre-update application file bytes.' }
    if ((Get-FileHash -LiteralPath (Join-Path $rollbackStatus.backup_path 'web/index.html') -Algorithm SHA256).Hash -cne $webIndexHashBeforeRollback) { throw 'Rollback backup did not capture the previous application file.' }
    if ((Get-FileHash -LiteralPath $userConfigPath -Algorithm SHA256).Hash -cne $configHashBefore) { throw 'Rollback changed user configuration.' }
    if ((Get-Content -LiteralPath $preservedDataPath -Raw) -cne 'user-data-must-survive-update') { throw 'Rollback modified or removed user data.' }
    if (-not $rollbackStatus.backup_path -or -not (Test-Path -LiteralPath (Join-Path $rollbackStatus.backup_path 'rollback-manifest.json') -PathType Leaf)) {
        throw 'The rollback attempt did not retain a recovery backup.'
    }

    Write-Host "Windows update apply smoke test passed: tamper rejection, validate-only, app-file replacement, stop/restart, /health, user-data preservation, retained backup and forced-failure rollback. Version=$Version Port=$port" -ForegroundColor Green
    $success = $true
} catch {
    Write-Host "Windows update apply smoke test failed: $($_.Exception.Message)" -ForegroundColor Red
    $diagnosticsRoot = Join-Path $env:GITHUB_WORKSPACE 'dist/update-smoke-diagnostics'
    New-Item -ItemType Directory -Force -Path $diagnosticsRoot | Out-Null
    foreach ($path in @(
        (Join-Path $dataRoot 'logs/update-apply.log'),
        (Join-Path $dataRoot 'logs/smoke-before.stdout.log'),
        (Join-Path $dataRoot 'logs/smoke-before.stderr.log'),
        (Join-Path $stageRoot 'apply-status.json')
    )) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Copy-Item -LiteralPath $path -Destination (Join-Path $diagnosticsRoot (Split-Path -Leaf $path)) -Force
        }
    }
    throw
} finally {
    Stop-SmokeRuntime
    if ($success) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Write-Host "Update smoke test workspace retained for diagnostics: $testRoot" -ForegroundColor Yellow
    }
}
