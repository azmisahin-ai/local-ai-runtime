[CmdletBinding()]
param(
    [ValidatePattern('^$|^[0-9a-fA-F]{40}$')]
    [string]$ExpectedCommitSha = '',
    [ValidateRange(30, 3600)]
    [int]$CiTimeoutSeconds = 1800,
    [ValidateRange(10, 120)]
    [int]$PollSeconds = 30
)

# Compatibility entry point. The only publisher implementation lives in
# scripts/Publish-LocalAIRuntime.ps1 and operates on this existing checkout.
$ErrorActionPreference = 'Stop'
$releaseScript = Join-Path $PSScriptRoot 'scripts/Publish-LocalAIRuntime.ps1'
if (-not (Test-Path -LiteralPath $releaseScript -PathType Leaf)) {
    throw "Repository publisher not found: $releaseScript"
}

$arguments = @{
    CiTimeoutSeconds = $CiTimeoutSeconds
    PollSeconds = $PollSeconds
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedCommitSha)) {
    $arguments.ExpectedCommitSha = $ExpectedCommitSha
}

& $releaseScript @arguments
