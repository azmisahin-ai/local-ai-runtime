[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InstallerPath,
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$HelperPath,
    [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$tagName = "v$Version"
$testRoot = Join-Path $env:GITHUB_WORKSPACE 'dist/desktop-update-smoke-data'
$dataRoot = Join-Path $testRoot 'user-data'
$stageRoot = Join-Path $dataRoot "updates/$tagName"
$stagedInstaller = Join-Path $stageRoot 'local-ai-runtime-windows-x64-setup.exe'
$manifestPath = Join-Path $stageRoot 'desktop-update-manifest.json'
$stageHelper = Join-Path $stageRoot 'Apply-LocalAIRuntimeDesktopUpdate.ps1'
$windowsPowerShell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'

function Quote-WindowsArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') { return $Value }
    $escaped = $Value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

function Invoke-DesktopUpdater([string[]]$Arguments, [int]$ExpectedExit = 0) {
    $allArguments = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $stageHelper) + $Arguments
    $argumentLine = ($allArguments | ForEach-Object { Quote-WindowsArgument ([string]$_) }) -join ' '
    $process = Start-Process -FilePath $windowsPowerShell -ArgumentList $argumentLine -WorkingDirectory $dataRoot -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne $ExpectedExit) { throw "Desktop updater exited with $($process.ExitCode), expected $ExpectedExit." }
    return $process.ExitCode
}

try {
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) { throw "Desktop installer is missing: $InstallerPath" }
    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) { throw "Desktop install root is missing: $InstallRoot" }
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
    Copy-Item -LiteralPath $InstallerPath -Destination $stagedInstaller -Force
    Copy-Item -LiteralPath $HelperPath -Destination $stageHelper -Force
    $packageInfo = Get-Item -LiteralPath $stagedInstaller
    $packageHash = (Get-FileHash -LiteralPath $stagedInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = [ordered]@{
        verified = $true
        tag_name = $tagName
        asset_name = 'local-ai-runtime-windows-x64-setup.exe'
        size_bytes = [int64]$packageInfo.Length
        sha256 = $packageHash
        release_url = "https://github.com/azmisahin-ai/local-ai-runtime/releases/tag/$tagName"
    }
    [System.IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4), [System.Text.UTF8Encoding]::new($false))

    # The mutable-data sentinel must survive both failed and successful setup runs.
    $dataSentinel = Join-Path $dataRoot 'desktop-update-user-data-sentinel.txt'
    [System.IO.File]::WriteAllText($dataSentinel, 'settings-engines-models-must-survive', [System.Text.UTF8Encoding]::new($false))
    $readmePath = Join-Path $InstallRoot 'README-INSTALL.txt'
    $failureSentinel = 'pre-update-content-must-return-after-failure'
    [System.IO.File]::WriteAllText($readmePath, $failureSentinel, [System.Text.UTF8Encoding]::new($false))

    $common = @(
        '-InstallRoot', $InstallRoot,
        '-DataRoot', $dataRoot,
        '-StageRoot', $stageRoot,
        '-InstallerPath', $stagedInstaller,
        '-ManifestPath', $manifestPath,
        '-TagName', $tagName,
        '-ExpectedSha256', $packageHash,
        '-CurrentVersion', $Version,
        '-DesktopPid', '0',
        '-Port', '65000',
        '-TestNoRestart'
    )

    # A changed installer must be rejected before shutdown, backup or setup.
    $originalInstallerBytes = [System.IO.File]::ReadAllBytes($stagedInstaller)
    try {
        $tamperedInstallerBytes = [byte[]]$originalInstallerBytes.Clone()
        $tamperedInstallerBytes[0] = $tamperedInstallerBytes[0] -bxor 0x01
        [System.IO.File]::WriteAllBytes($stagedInstaller, $tamperedInstallerBytes)
        Invoke-DesktopUpdater ($common + @('-TestForceFailureAfterInstall')) 0 | Out-Null
        $status = Get-Content -LiteralPath (Join-Path $stageRoot 'desktop-update-status.json') -Raw | ConvertFrom-Json -ErrorAction Stop
        if ([string]$status.state -ne 'failed' -or [string]$status.message -notmatch 'SHA-256 changed') {
            throw "A tampered desktop installer was not rejected before apply: state=$($status.state) message=$($status.message)"
        }
        if ([System.IO.File]::ReadAllText($readmePath) -cne $failureSentinel) { throw 'A tampered desktop installer changed the installed application.' }
        if ([System.IO.File]::ReadAllText($dataSentinel) -cne 'settings-engines-models-must-survive') { throw 'A tampered desktop installer changed user data.' }
    } finally {
        [System.IO.File]::WriteAllBytes($stagedInstaller, $originalInstallerBytes)
    }

    # Fault injection after Inno Setup has replaced the files. The full prior
    # application directory must be restored byte-for-byte, not just the runtime.
    Invoke-DesktopUpdater ($common + @('-TestForceFailureAfterInstall')) 0 | Out-Null
    $status = Get-Content -LiteralPath (Join-Path $stageRoot 'desktop-update-status.json') -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$status.state -ne 'rolled_back') { throw "Forced setup failure did not roll back: state=$($status.state) message=$($status.message)" }
    $backupManifestPath = Join-Path ([string]$status.backup_path) 'backup-manifest.json'
    if (-not (Test-Path -LiteralPath $backupManifestPath -PathType Leaf)) { throw 'The completed desktop rollback did not retain its backup manifest.' }
    $backupManifest = Get-Content -LiteralPath $backupManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $backupEntries = @($backupManifest.files)
    if ([int]$backupManifest.schema_version -ne 1 -or
        [int]$backupManifest.file_count -ne $backupEntries.Count -or
        $backupEntries.Count -lt 4 -or $backupEntries.Count -gt 4096) {
        throw "Desktop rollback manifest round-trip failed: schema=$($backupManifest.schema_version) declared=$($backupManifest.file_count) parsed=$($backupEntries.Count)."
    }
    if ([System.IO.File]::ReadAllText($readmePath) -cne $failureSentinel) { throw 'The full-install rollback did not restore the pre-update application file.' }
    if ([System.IO.File]::ReadAllText($dataSentinel) -cne 'settings-engines-models-must-survive') { throw 'The failed desktop update changed user data.' }

    # Simulate a machine/process interruption immediately after the setup
    # installer returns. Startup recovery must validate and restore the saved
    # full-directory backup rather than leaving a half-finished install.
    $crashSentinel = 'pre-crash-content-must-return-after-recovery'
    [System.IO.File]::WriteAllText($readmePath, $crashSentinel, [System.Text.UTF8Encoding]::new($false))
    Invoke-DesktopUpdater ($common + @('-TestCrashAfterInstall')) 87 | Out-Null
    Invoke-DesktopUpdater @(
        '-InstallRoot', $InstallRoot,
        '-DataRoot', $dataRoot,
        '-StageRoot', $stageRoot,
        '-DesktopPid', '0',
        '-Port', '65000',
        '-RecoverInterrupted',
        '-TestNoRestart'
    ) 0 | Out-Null
    $status = Get-Content -LiteralPath (Join-Path $stageRoot 'desktop-update-status.json') -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$status.state -ne 'rolled_back') { throw "Interrupted desktop update was not recovered: state=$($status.state) message=$($status.message)" }
    if ([System.IO.File]::ReadAllText($readmePath) -cne $crashSentinel) { throw 'Interrupted desktop update recovery did not restore the pre-update application file.' }
    if ([System.IO.File]::ReadAllText($dataSentinel) -cne 'settings-engines-models-must-survive') { throw 'Interrupted desktop update recovery changed user data.' }

    # Run a successful same-version setup fixture without launching a WinForms
    # window on the CI service session. The interactive health check is covered
    # by clean-user-session acceptance and the normal runtime package smoke.
    Invoke-DesktopUpdater ($common + @('-TestSkipHealthCheck')) 0 | Out-Null
    $status = Get-Content -LiteralPath (Join-Path $stageRoot 'desktop-update-status.json') -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$status.state -ne 'succeeded') { throw "Successful desktop setup fixture did not finish: state=$($status.state) message=$($status.message)" }
    if ([System.IO.File]::ReadAllText($dataSentinel) -cne 'settings-engines-models-must-survive') { throw 'Successful desktop update changed user data.' }
    & (Join-Path $InstallRoot 'LocalAIRuntime.Desktop.exe') '--verify-install'
    if ($LASTEXITCODE -ne 0) { throw 'Desktop payload verification failed after a successful setup update.' }

    Write-Host "Windows desktop-update smoke passed: official setup SHA-256, full-install backup, forced-failure rollback, interrupted-update recovery, successful setup payload, and user-data preservation. Version=$Version" -ForegroundColor Green
    Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
} catch {
    Write-Host "Windows desktop-update smoke failed: $($_.Exception.Message)" -ForegroundColor Red
    throw
}
