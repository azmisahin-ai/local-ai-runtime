[CmdletBinding()]
param(
    [ValidatePattern('^$|^[0-9a-fA-F]{40}$')]
    [string]$ExpectedCommitSha = '',
    [ValidateRange(30, 3600)]
    [int]$CiTimeoutSeconds = 1800,
    [ValidateRange(10, 120)]
    [int]$PollSeconds = 30
)

# Canonical repository-local publisher. It safely fast-forwards the current
# clean main branch to GitHub, waits for main CI, then creates and verifies the
# next alpha release. No bundle, Downloads clone, or bootstrap script is needed.
$ErrorActionPreference = 'Stop'
$repositorySlug = 'azmisahin-ai/local-ai-runtime'
$repositoryUrl = 'https://github.com/azmisahin-ai/local-ai-runtime.git'
$actionsUrl = 'https://github.com/azmisahin-ai/local-ai-runtime/actions'
$apiHeaders = @{
    Accept = 'application/vnd.github+json'
    'User-Agent' = 'LocalAIRuntime-Publisher'
    'X-GitHub-Api-Version' = '2022-11-28'
}
# Public repository diagnostics do not require a token. When a user already has
# a token in the environment, use it to support private forks without prompting.
$githubToken = if (-not [string]::IsNullOrWhiteSpace($env:GH_TOKEN)) {
    $env:GH_TOKEN
} elseif (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
    $env:GITHUB_TOKEN
} else { $null }
if ($githubToken) { $apiHeaders['Authorization'] = "Bearer $githubToken" }
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

function Show-WorkflowFailureDiagnostics {
    param(
        [Parameter(Mandatory = $true)]$Run,
        [Parameter(Mandatory = $true)][string]$StageName
    )

    $runId = [string]$Run.id
    if ([string]::IsNullOrWhiteSpace($runId)) {
        Write-Warning 'GitHub did not return a workflow run ID; automatic log collection is unavailable.'
        return
    }

    $downloads = Join-Path $HOME 'Downloads'
    $diagnosticsRoot = Join-Path $downloads 'local-ai-runtime-ci-diagnostics'
    $runDirectory = Join-Path $diagnosticsRoot ("run-{0}" -f $runId)
    New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

    Write-Host ''
    Write-Host '================ CI FAILURE DIAGNOSTICS ================' -ForegroundColor Red
    Write-Host "Stage: $StageName"
    Write-Host "Workflow run: $($Run.html_url)"
    Write-Host "Commit: $($Run.head_sha)"
    Write-Host "Full job logs: $runDirectory" -ForegroundColor Green

    $jobsUri = "https://api.github.com/repos/$repositorySlug/actions/runs/$runId/jobs?filter=latest&per_page=100"
    try {
        $jobsResponse = Invoke-RestMethod -Method Get -Uri $jobsUri -Headers $apiHeaders -TimeoutSec 30
    } catch {
        Write-Warning "Could not list failed jobs: $($_.Exception.Message)"
        Write-Warning "Open the run directly: $($Run.html_url)"
        Write-Host '=========================================================' -ForegroundColor Red
        return
    }

    $jobs = @($jobsResponse.jobs)
    $problemJobs = @($jobs | Where-Object {
        $_.conclusion -and $_.conclusion -notin @('success', 'skipped', 'neutral')
    })
    if ($problemJobs.Count -eq 0) {
        Write-Warning 'The run failed, but the jobs API did not return a non-success job. Showing all returned jobs.'
        $problemJobs = $jobs
    }

    foreach ($job in $problemJobs) {
        Write-Host ''
        Write-Host ("[{0}] {1} — {2}" -f $job.conclusion, $job.name, $job.html_url) -ForegroundColor Yellow
        $failedSteps = @($job.steps | Where-Object {
            $_.conclusion -and $_.conclusion -notin @('success', 'skipped', 'neutral')
        })
        if ($failedSteps.Count -gt 0) {
            foreach ($step in $failedSteps) {
                Write-Host ("  Step {0}: {1} ({2})" -f $step.number, $step.name, $step.conclusion) -ForegroundColor Red
            }
        } else {
            Write-Host '  No failed step details were returned by the jobs API.' -ForegroundColor DarkYellow
        }

        # Check-run annotations often contain compiler/test diagnostics with
        # file and line numbers, so show them before the raw log excerpt.
        if (-not [string]::IsNullOrWhiteSpace([string]$job.check_run_url)) {
            try {
                $annotationsUri = $job.check_run_url.TrimEnd('/') + '/annotations?per_page=100'
                $annotations = @(Invoke-RestMethod -Method Get -Uri $annotationsUri -Headers $apiHeaders -TimeoutSec 20)
                foreach ($annotation in $annotations) {
                    $location = ''
                    if ($annotation.path) {
                        $location = [string]$annotation.path
                        if ($annotation.start_line) { $location += ":$($annotation.start_line)" }
                    }
                    Write-Host ("  Annotation {0} {1}: {2}" -f $annotation.annotation_level, $location, $annotation.message) -ForegroundColor Red
                }
            } catch {
                Write-Host "  Check annotations unavailable: $($_.Exception.Message)" -ForegroundColor DarkYellow
            }
        }

        $logPath = Join-Path $runDirectory ("job-{0}.log" -f $job.id)
        $logUri = "https://api.github.com/repos/$repositorySlug/actions/jobs/$($job.id)/logs"
        try {
            # GitHub redirects this endpoint to a short-lived log download URL.
            # Invoke-WebRequest follows the redirect and writes the plain-text job log.
            Invoke-WebRequest -Method Get -Uri $logUri -Headers $apiHeaders -OutFile $logPath -UseBasicParsing -TimeoutSec 90 | Out-Null
            if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
                throw 'GitHub returned no log file.'
            }
            Write-Host "  Saved full log: $logPath" -ForegroundColor Green

            $lines = @(Get-Content -LiteralPath $logPath)
            $pattern = '(?i)(\berror\b|\bfatal\b|\bfailed\b|\bfailure\b|\bexception\b|\btraceback\b|permissionerror|assertionerror|\bC[0-9]{4}\b|ninja: build stopped|errors while running ctest|process completed with exit code)'
            $matchIndexes = @()
            for ($index = 0; $index -lt $lines.Count; $index++) {
                if ([string]$lines[$index] -match $pattern) { $matchIndexes += $index }
            }
            if ($matchIndexes.Count -eq 0) {
                Write-Host '  No standard error markers found; final log lines:' -ForegroundColor DarkYellow
                $start = [Math]::Max(0, $lines.Count - 25)
                for ($index = $start; $index -lt $lines.Count; $index++) {
                    Write-Host ("    {0,6}: {1}" -f ($index + 1), $lines[$index])
                }
            } else {
                Write-Host '  Relevant error lines (full log is saved above):' -ForegroundColor Cyan
                $wanted = @{}
                foreach ($hit in @($matchIndexes | Select-Object -First 60)) {
                    for ($context = [Math]::Max(0, $hit - 2); $context -le [Math]::Min($lines.Count - 1, $hit + 2); $context++) {
                        $wanted[$context] = $true
                    }
                }
                $shown = 0
                foreach ($index in @($wanted.Keys | Sort-Object)) {
                    Write-Host ("    {0,6}: {1}" -f ([int]$index + 1), $lines[[int]$index])
                    $shown++
                    if ($shown -ge 120) {
                        Write-Host '    ... excerpt capped; inspect the full log file for remaining lines.' -ForegroundColor DarkYellow
                        break
                    }
                }
            }
        } catch {
            Write-Warning "Could not download job $($job.id) log: $($_.Exception.Message)"
            Write-Warning "Open this job in GitHub: $($job.html_url)"
        }
    }

    Write-Host ''
    Write-Host "CI diagnostics saved under: $runDirectory" -ForegroundColor Green
    Write-Host '=========================================================' -ForegroundColor Red
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
                try {
                    Show-WorkflowFailureDiagnostics -Run $run -StageName $StageName
                } catch {
                    Write-Warning "Automatic CI diagnostics failed: $($_.Exception.Message)"
                    Write-Warning "Open GitHub Actions for the full result: $($run.html_url)"
                }
                if ($ExpectedRef -eq 'main') {
                    $diagnosticsRoot = Join-Path (Join-Path $HOME 'Downloads') 'local-ai-runtime-ci-diagnostics'
                    $runDiagnostics = Join-Path $diagnosticsRoot ("run-{0}" -f $run.id)
                    throw "Main CI finished with conclusion '$($run.conclusion)'. No release tag was created. Diagnostics: $runDiagnostics. Review: $($run.html_url)"
                }
                throw "Release CI for $ExpectedRef finished with conclusion '$($run.conclusion)'. The tag remains on GitHub, but a successful Release was not confirmed. Do not move or reuse the tag. Review diagnostics and run: $($run.html_url)"
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
    if (-not [string]::IsNullOrWhiteSpace($ExpectedCommitSha)) {
        $expectedSha = $ExpectedCommitSha.ToLowerInvariant()
        if ($commitSha -ne $expectedSha) {
            throw "The current checkout is $($commitSha.Substring(0, 7)), not the requested commit $($expectedSha.Substring(0, 7)). No push or release tag was created."
        }
    }

    $remoteSha = (Invoke-Git -GitArgs @('rev-parse', 'origin/main')).ToLowerInvariant()
    if ($remoteSha -ne $commitSha) {
        & git merge-base --is-ancestor origin/main HEAD
        $localIsAhead = ($LASTEXITCODE -eq 0)
        if (-not $localIsAhead) {
            & git merge-base --is-ancestor HEAD origin/main
            if ($LASTEXITCODE -eq 0) {
                throw 'GitHub main is ahead of this checkout. Run git pull --ff-only origin main, review the updated code, then retry. No push or release tag was created.'
            }
            throw 'Local main and GitHub main have diverged. Resolve the history explicitly; this publisher never resets or force-pushes. No release tag was created.'
        }
        Write-Host "Pushing local main to GitHub (fast-forward only): $($commitSha.Substring(0, 7))" -ForegroundColor Cyan
        Invoke-Git -GitArgs @('push', 'origin', 'main') | Out-Null
    } else {
        Write-Host 'This exact commit is already on GitHub main.' -ForegroundColor Yellow
    }

    # Re-read the remote after push. A concurrent main update must block tagging.
    Invoke-Git -GitArgs @('fetch', '--tags', 'origin', '+refs/heads/main:refs/remotes/origin/main') | Out-Null
    $remoteSha = (Invoke-Git -GitArgs @('rev-parse', 'origin/main')).ToLowerInvariant()
    if ($remoteSha -ne $commitSha) {
        throw 'GitHub main does not match the local commit after push. No version tag will be created.'
    }
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
