[CmdletBinding()]
param(
    [string]$BundlePath = (Join-Path $HOME 'Downloads\local-ai-runtime-update.bundle'),
    [string]$RepositoryPath = (Join-Path $HOME 'Downloads\local-ai-runtime-real-engine-push'),
    [ValidateRange(30, 3600)]
    [int]$CiTimeoutSeconds = 1800,
    [ValidateRange(10, 120)]
    [int]$PollSeconds = 30
)

# Stable bootstrap: import the prepared bundle without changing commit IDs,
# push main by fast-forward only, then delegate CI/release gating to the script
# shipped in that exact bundle. Keep this filename and command stable for users.
$ErrorActionPreference = 'Stop'
$repositoryUrl = 'https://github.com/azmisahin-ai/local-ai-runtime.git'

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$GitArgs)
    $output = & git @GitArgs 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $details = ($output | Out-String).Trim()
        throw "git $($GitArgs -join ' ') failed with exit code $exitCode.`n$details"
    }
    return (($output | Out-String).Trim())
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git was not found. Install Git for Windows and reopen PowerShell.'
}
if (-not (Test-Path -LiteralPath $BundlePath -PathType Leaf)) {
    throw "Update bundle not found: $BundlePath`nSave the prepared file as 'local-ai-runtime-update.bundle' in Downloads, then run this same command again."
}
$BundlePath = (Resolve-Path -LiteralPath $BundlePath).Path

if (-not (Test-Path -LiteralPath $RepositoryPath)) {
    $parent = Split-Path -Parent $RepositoryPath
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Write-Host '1/6 Creating the standard project working folder...' -ForegroundColor Cyan
    & git clone $repositoryUrl $RepositoryPath
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone the repository. Check GitHub access and credentials.' }
} elseif (-not (Test-Path -LiteralPath (Join-Path $RepositoryPath '.git'))) {
    throw "The standard working folder exists but is not a Git repository: $RepositoryPath`nIt was not changed. Rename it or pass -RepositoryPath with another path."
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Push-Location $RepositoryPath
try {
    $branch = Invoke-Git -GitArgs @('branch', '--show-current')
    if ($branch -ne 'main') { throw "The working folder is on '$branch', not 'main'. Nothing was reset or pushed." }
    $status = Invoke-Git -GitArgs @('status', '--porcelain')
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        throw "The working tree is not clean. Nothing was overwritten or pushed.`n$status`nResolve these changes, then retry."
    }

    $remotes = Invoke-Git -GitArgs @('remote')
    if (($remotes -split '\r?\n') -contains 'origin') {
        Invoke-Git -GitArgs @('remote', 'set-url', 'origin', $repositoryUrl) | Out-Null
    } else {
        Invoke-Git -GitArgs @('remote', 'add', 'origin', $repositoryUrl) | Out-Null
    }

    Write-Host '2/6 Fetching GitHub main and version tags...' -ForegroundColor Cyan
    Invoke-Git -GitArgs @('fetch', '--tags', 'origin', '+refs/heads/main:refs/remotes/origin/main') | Out-Null

    Write-Host '3/6 Verifying the prepared bundle...' -ForegroundColor Cyan
    Invoke-Git -GitArgs @('bundle', 'verify', $BundlePath) | Out-Null
    $headsOutput = Invoke-Git -GitArgs @('bundle', 'list-heads', $BundlePath)
    $mainHeads = @($headsOutput -split '\r?\n' | Where-Object { $_ -match ' refs/heads/main$' })
    if ($mainHeads.Count -ne 1) {
        throw 'The bundle must contain exactly one refs/heads/main entry. No push was attempted.'
    }
    if ($mainHeads[0] -match '^([0-9a-fA-F]{40})\s+refs/heads/main$') {
        $bundleShaFromHeader = $Matches[1].ToLowerInvariant()
    } else {
        throw 'The bundle main ref does not contain a valid full commit ID. No push was attempted.'
    }
    Invoke-Git -GitArgs @('fetch', $BundlePath, '+refs/heads/main:refs/remotes/prepared/main') | Out-Null
    $bundleSha = (Invoke-Git -GitArgs @('rev-parse', 'refs/remotes/prepared/main')).ToLowerInvariant()
    if ($bundleSha -ne $bundleShaFromHeader) { throw 'The bundle commit changed during import. No push was attempted.' }

    & git merge-base --is-ancestor origin/main refs/remotes/prepared/main
    if ($LASTEXITCODE -ne 0) {
        & git merge-base --is-ancestor refs/remotes/prepared/main origin/main
        if ($LASTEXITCODE -eq 0) {
            throw 'This bundle is older than GitHub main. Download the newest bundle; nothing was pushed.'
        }
        throw 'The prepared bundle and GitHub main have diverged. No reset or force-push was attempted.'
    }

    & git merge-base --is-ancestor HEAD origin/main
    if ($LASTEXITCODE -eq 0) {
        Invoke-Git -GitArgs @('merge', '--ff-only', 'origin/main') | Out-Null
    } else {
        & git merge-base --is-ancestor HEAD refs/remotes/prepared/main
        if ($LASTEXITCODE -ne 0) {
            throw 'Local main has commits that are not on GitHub main or in the bundle. Nothing was reset or overwritten.'
        }
    }
    Invoke-Git -GitArgs @('merge', '--ff-only', 'refs/remotes/prepared/main') | Out-Null
    $commitSha = (Invoke-Git -GitArgs @('rev-parse', 'HEAD')).ToLowerInvariant()
    if ($commitSha -ne $bundleSha) { throw 'Local main does not match the exact bundle commit. Refusing to push.' }
    $message = Invoke-Git -GitArgs @('log', '-1', '--pretty=%s')
    Write-Host "Prepared commit: $($commitSha.Substring(0, 7)) $message" -ForegroundColor Green

    $remoteSha = (Invoke-Git -GitArgs @('rev-parse', 'origin/main')).ToLowerInvariant()
    if ($commitSha -ne $remoteSha) {
        Write-Host '4/6 Pushing main with a normal fast-forward push...' -ForegroundColor Cyan
        Invoke-Git -GitArgs @('push', 'origin', 'main') | Out-Null
    } else {
        Write-Host '4/6 This exact commit is already on GitHub main; continuing the release checks.' -ForegroundColor Yellow
    }

    $releaseScript = Join-Path $RepositoryPath 'scripts\Publish-LocalAIRuntime.ps1'
    if (-not (Test-Path -LiteralPath $releaseScript -PathType Leaf)) {
        throw "The prepared bundle does not contain its release gate script: $releaseScript. main was pushed if it was new; review GitHub Actions before retrying."
    }
    Write-Host '5/6 Handing release checks to the script shipped in this exact commit...' -ForegroundColor Cyan
    & $releaseScript -ExpectedCommitSha $commitSha -CiTimeoutSeconds $CiTimeoutSeconds -PollSeconds $PollSeconds

    Write-Host '6/6 Publishing flow complete. Use the Actions and Release links printed above.' -ForegroundColor Green
} finally {
    Pop-Location
}
