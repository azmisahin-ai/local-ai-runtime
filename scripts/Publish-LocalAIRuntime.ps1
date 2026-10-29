[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedCommitSha,
    [ValidateRange(30, 3600)]
    [int]$CiTimeoutSeconds = 1800,
    [ValidateRange(10, 120)]
    [int]$PollSeconds = 30
)

# This release gate is intentionally invoked by the stable root-level
# Publish-LocalAIRuntime.ps1 after it imports and pushes this exact commit.
$ErrorActionPreference = 'Stop'
$repositorySlug = 'azmisahin-ai/local-ai-runtime'
$repositoryUrl = 'https://github.com/azmisahin-ai/local-ai-runtime.git'
$actionsUrl = 'https://github.com/azmisahin-ai/local-ai-runtime/actions'
$apiHeaders = @{
    Accept = 'application/vnd.github+json'
    'User-Agent' = 'LocalAIRuntime-Publisher'
    'X-GitHub-Api-Version' = '2022-11-28'
}
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Invoke-Git {
    param(
        [Parameter(Mandatory = $true)][string[]]$GitArgs,
        [switch]$AllowNonZeroExit
    )

    # Windows PowerShell 5.1 can promote native stderr text to a terminating
    # NativeCommandError when ErrorActionPreference is Stop, even if Git exits
    # successfully (for example, `git bundle verify` prints "is okay" to stderr).
    # Temporarily use Continue while capturing both streams, then trust Git's
    # actual exit code. Restore the caller's strict preference immediately.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& git @GitArgs 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    $details = (@($output) | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    if ($exitCode -ne 0) {
        if ($AllowNonZeroExit) { return '' }
        throw "git $($GitArgs -join ' ') failed with exit code $exitCode.`n$details"
    }
    return $details.Trim()
}

function Get-NextAlphaTag {
    $tagOutput = Invoke-Git -GitArgs @('tag', '--list', 'v*')
    $versions = @()
    foreach ($tag in @($tagOutput -split '\r?\n')) {
        if ($tag -match '^v(\d+)\.(\d+)\.(\d+)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {
            $versions += [pscustomobject]@{
                Major = [int]$Matches[1]
                Minor = [int]$Matches[2]
                Patch = [int]$Matches[3]
            }
        }
    }
    if ($versions.Count -eq 0) { return 'v0.1.0-alpha' }
    $latest = $versions | Sort-Object -Property Major, Minor, Patch | Select-Object -Last 1
    return ('v{0}.{1}.{2}-alpha' -f $latest.Major, $latest.Minor, ($latest.Patch + 1))
}

function Wait-ForPipelineRun {
    param(
        [Parameter(Mandatory = $true)][string]$ExpectedSha,
        [Parameter(Mandatory = $true)][string]$ExpectedRef,
        [Parameter(Mandatory = $true)][string]$StageName
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($CiTimeoutSeconds)
    $consecutiveApiErrors = 0
    $url = "https://api.github.com/repos/$repositorySlug/actions/workflows/pipeline.yml/runs?head_sha=$ExpectedSha&per_page=50"
    Write-Host "Waiting for $StageName CI on $ExpectedRef ($($ExpectedSha.Substring(0, 7)))..." -ForegroundColor Cyan

    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $response = Invoke-RestMethod -Method Get -Uri $url -Headers $apiHeaders -TimeoutSec 20
            $consecutiveApiErrors = 0
        } catch {
            $consecutiveApiErrors++
            Write-Warning "Could not query GitHub Actions status ($consecutiveApiErrors consecutive errors): $($_.Exception.Message)"
            if ($consecutiveApiErrors -ge 5) { throw "Unable to query GitHub Actions. Open $actionsUrl and verify the run manually." }
            Start-Sleep -Seconds $PollSeconds
            continue
        }

        $matchingRuns = @($response.workflow_runs | Where-Object {
            $_.head_sha -eq $ExpectedSha -and $_.head_branch -eq $ExpectedRef -and $_.event -eq 'push'
        } | Sort-Object -Property created_at -Descending)

        if ($matchingRuns.Count -gt 0) {
            $run = $matchingRuns[0]
            if ($run.status -eq 'completed') {
                if ($run.conclusion -eq 'success') {
                    Write-Host "$StageName CI passed: $($run.html_url)" -ForegroundColor Green
                    return $run
                }
                if ($ExpectedRef -eq 'main') {
                    throw "Main CI finished with conclusion '$($run.conclusion)'. No release tag was created. Review: $($run.html_url)"
                }
                throw "Release CI for $ExpectedRef finished with conclusion '$($run.conclusion)'. The tag remains on GitHub, but a successful Release was not confirmed. Do not move or reuse the tag. Review: $($run.html_url)"
            }
            $createdAt = [DateTimeOffset]::Parse([string]$run.created_at).ToUniversalTime()
            $elapsed = [DateTimeOffset]::UtcNow - $createdAt
            $elapsedText = '{0:00}:{1:00}:{2:00}' -f [math]::Floor($elapsed.TotalHours), $elapsed.Minutes, $elapsed.Seconds
            Write-Host ("  Status: {0}; elapsed: {1}" -f $run.status, $elapsedText)
        } else {
            Write-Host '  Waiting for GitHub to register the workflow run...'
        }
        Start-Sleep -Seconds $PollSeconds
    }

    if ($ExpectedRef -eq 'main') {
        throw "Main CI status did not complete within $CiTimeoutSeconds seconds. Check $actionsUrl. No release tag was created by this script."
    }
    throw "Release CI status for $ExpectedRef did not complete within $CiTimeoutSeconds seconds. The tag was already pushed; check $actionsUrl and do not move or reuse that tag."
}

function Get-VersionTagsAtCommit([string]$CommitSha) {
    $output = Invoke-Git -GitArgs @('tag', '--points-at', $CommitSha)
    return @($output -split '\r?\n' | Where-Object { $_ -match '^v\d+\.\d+\.\d+' })
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git was not found.' }
$root = Invoke-Git -GitArgs @('rev-parse', '--show-toplevel')
Push-Location $root
try {
    $branch = Invoke-Git -GitArgs @('branch', '--show-current')
    if ($branch -ne 'main') { throw "Release gate expected branch main, found '$branch'." }
    $status = Invoke-Git -GitArgs @('status', '--porcelain')
    if (-not [string]::IsNullOrWhiteSpace($status)) { throw "Working tree must be clean before release gating.`n$status" }

    $remoteNames = Invoke-Git -GitArgs @('remote')
    if (($remoteNames -split '\r?\n') -contains 'origin') {
        Invoke-Git -GitArgs @('remote', 'set-url', 'origin', $repositoryUrl) | Out-Null
    } else {
        Invoke-Git -GitArgs @('remote', 'add', 'origin', $repositoryUrl) | Out-Null
    }
    Invoke-Git -GitArgs @('fetch', '--tags', 'origin', '+refs/heads/main:refs/remotes/origin/main') | Out-Null

    $commitSha = (Invoke-Git -GitArgs @('rev-parse', 'HEAD')).ToLowerInvariant()
    $expectedSha = $ExpectedCommitSha.ToLowerInvariant()
    if ($commitSha -ne $expectedSha) { throw "The working copy is $($commitSha.Substring(0, 7)), but bootstrap imported $($expectedSha.Substring(0, 7)). Refusing to create a release tag." }
    $remoteSha = (Invoke-Git -GitArgs @('rev-parse', 'origin/main')).ToLowerInvariant()
    if ($remoteSha -ne $commitSha) { throw 'GitHub main does not match the prepared commit. No version tag will be created.' }
    $commitMessage = Invoke-Git -GitArgs @('log', '-1', '--pretty=%s')
    Write-Host "Publishing commit: $($commitSha.Substring(0, 7)) $commitMessage" -ForegroundColor Green

    Write-Host '1/4 Waiting for all platform jobs and GitHub Pages...' -ForegroundColor Cyan
    Wait-ForPipelineRun -ExpectedSha $commitSha -ExpectedRef 'main' -StageName 'main branch' | Out-Null

    Write-Host '2/4 Reconfirming main did not move during CI...' -ForegroundColor Cyan
    Invoke-Git -GitArgs @('fetch', '--tags', 'origin', '+refs/heads/main:refs/remotes/origin/main') | Out-Null
    $remoteShaAfterCi = (Invoke-Git -GitArgs @('rev-parse', 'origin/main')).ToLowerInvariant()
    if ($remoteShaAfterCi -ne $commitSha) { throw 'GitHub main advanced during CI. No release tag was created.' }

    $tagsAtCommit = @(Get-VersionTagsAtCommit $commitSha)
    if ($tagsAtCommit.Count -gt 0) {
        $nextTag = $tagsAtCommit[0]
        $remoteTagLine = Invoke-Git -GitArgs @('ls-remote', '--tags', 'origin', "refs/tags/$nextTag")
        if ([string]::IsNullOrWhiteSpace($remoteTagLine)) {
            Write-Host "3/4 Publishing existing local tag $nextTag without moving it..." -ForegroundColor Yellow
            Invoke-Git -GitArgs @('push', 'origin', $nextTag) | Out-Null
        }
    } else {
        $nextTag = Get-NextAlphaTag
        if (-not [string]::IsNullOrWhiteSpace((Invoke-Git -GitArgs @('ls-remote', '--tags', 'origin', "refs/tags/$nextTag")))) {
            throw "Release tag $nextTag already exists remotely and points elsewhere. It will not be moved or reused."
        }
        Write-Host "3/4 Main CI is green. Creating next alpha tag: $nextTag" -ForegroundColor Cyan
        $taggerName = Invoke-Git -GitArgs @('config', '--get', 'user.name') -AllowNonZeroExit
        $taggerEmail = Invoke-Git -GitArgs @('config', '--get', 'user.email') -AllowNonZeroExit
        if ([string]::IsNullOrWhiteSpace($taggerName)) { Invoke-Git -GitArgs @('config', 'user.name', 'Local AI Runtime Publisher') | Out-Null }
        if ([string]::IsNullOrWhiteSpace($taggerEmail)) { Invoke-Git -GitArgs @('config', 'user.email', 'local-ai-runtime-publisher@users.noreply.github.com') | Out-Null }
        Invoke-Git -GitArgs @('tag', '-a', $nextTag, $commitSha, '-m', "Local AI Runtime $nextTag") | Out-Null
        Invoke-Git -GitArgs @('push', 'origin', $nextTag) | Out-Null
    }

    Write-Host '4/4 Waiting for tagged CI and GitHub Release publication...' -ForegroundColor Cyan
    $tagRun = Wait-ForPipelineRun -ExpectedSha $commitSha -ExpectedRef $nextTag -StageName "release $nextTag"
    Write-Host ''
    Write-Host 'PUBLISH COMPLETE' -ForegroundColor Green
    Write-Host "Commit:  $($commitSha.Substring(0, 7))"
    Write-Host "Release: https://github.com/$repositorySlug/releases/tag/$nextTag"
    Write-Host "Actions: $($tagRun.html_url)"
    Write-Host 'The site follows the successful main CI; public release downloads update after the tagged CI completes.' -ForegroundColor Green
} finally {
    Pop-Location
}
