[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$HelperPath,
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [Parameter(Mandatory = $true)][string]$DataRoot,
    [Parameter(Mandatory = $true)][string]$StageRoot,
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$TagName,
    [Parameter(Mandatory = $true)][string]$AssetName,
    [Parameter(Mandatory = $true)][string]$ExpectedSha256,
    [Parameter(Mandatory = $true)][int]$Port,
    [Parameter(Mandatory = $true)][string]$CurrentVersion
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Avoid relying on optional utility-module auto-loading in the inbox Windows
# PowerShell 5.1 process. Use the .NET cryptography API directly, matching the
# updater helper's SHA-256 implementation and the framework shipped with 5.1.
function Get-FileSha256Hex([string]$LiteralPath) {
    if ([string]::IsNullOrWhiteSpace($LiteralPath)) {
        throw 'A file path is required when calculating a SHA-256 digest.'
    }

    $fileStream = $null
    $sha256 = $null
    try {
        $fileStream = [System.IO.File]::OpenRead($LiteralPath)
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        $digestBytes = $sha256.ComputeHash($fileStream)
        return ([System.BitConverter]::ToString($digestBytes)).Replace('-', '')
    } finally {
        if ($null -ne $fileStream) { $fileStream.Dispose() }
        if ($null -ne $sha256) { $sha256.Dispose() }
    }
}
if ($PSVersionTable.PSVersion.Major -ne 5 -or $PSVersionTable.PSVersion.Minor -ne 1) {
    throw "This regression test must run under Windows PowerShell 5.1; current version is $($PSVersionTable.PSVersion)."
}
foreach ($path in @($HelperPath, $InstallRoot, $DataRoot, $StageRoot, $PackagePath, $ManifestPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required ValidateOnly test path is missing: $path" }
}

# Verify the .NET hashing wrapper against the standard SHA-256 test vector
# before using it to assert that ValidateOnly preserved installed/user files.
$sha256ProbePath = Join-Path $StageRoot 'powershell51-sha256-selftest.tmp'
try {
    [System.IO.File]::WriteAllBytes($sha256ProbePath, [System.Text.Encoding]::ASCII.GetBytes('abc'))
    $probeDigest = Get-FileSha256Hex -LiteralPath $sha256ProbePath
    if ($probeDigest -cne 'BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD') {
        throw "The Windows PowerShell 5.1 .NET SHA-256 self-test returned an unexpected digest: $probeDigest"
    }
} finally {
    Remove-Item -LiteralPath $sha256ProbePath -Force -ErrorAction SilentlyContinue
}

$runtimePath = Join-Path $InstallRoot 'local-ai-runtime.exe'
$configPath = Join-Path $DataRoot 'config/engines.json'
$runtimeHashBefore = (Get-FileSha256Hex -LiteralPath $runtimePath)
$configHashBefore = (Get-FileSha256Hex -LiteralPath $configPath)
$statusPath = Join-Path $StageRoot 'apply-status.json'
$baseArgs = @(
    '-InstallRoot', $InstallRoot,
    '-DataRoot', $DataRoot,
    '-StageRoot', $StageRoot,
    '-PackagePath', $PackagePath,
    '-ManifestPath', $ManifestPath,
    '-TagName', $TagName,
    '-AssetName', $AssetName,
    '-ExpectedSha256', $ExpectedSha256,
    '-RuntimePid', '0',
    '-Port', [string]$Port,
    '-CurrentVersion', $CurrentVersion,
    '-ValidateOnly'
)

# A valid archive must pass validation even with RuntimePid=0, and must not
# stop a process or change application/user data in Windows PowerShell 5.1.
& $HelperPath @baseArgs
$status = Get-Content -LiteralPath $statusPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
if ([string]$status.state -ne 'validated') {
    throw "Windows PowerShell 5.1 did not accept the valid validate-only package: state=$($status.state) message=$($status.message)"
}
if ((Get-FileSha256Hex -LiteralPath $runtimePath) -cne $runtimeHashBefore) {
    throw 'Windows PowerShell 5.1 validate-only changed the installed runtime executable.'
}
if ((Get-FileSha256Hex -LiteralPath $configPath) -cne $configHashBefore) {
    throw 'Windows PowerShell 5.1 validate-only changed user configuration.'
}

# A syntactically valid but incorrect digest must fail closed under 5.1 too.
$tamperedDigest = '0' * 64
if ($tamperedDigest -ceq $ExpectedSha256) { $tamperedDigest = 'f' * 64 }
$manifestOriginalText = [System.IO.File]::ReadAllText($ManifestPath)
$manifestObject = $manifestOriginalText | ConvertFrom-Json -ErrorAction Stop
$manifestObject.sha256 = $tamperedDigest
$manifestText = $manifestObject | ConvertTo-Json -Depth 4
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($ManifestPath, $manifestText, $utf8NoBom)
$tamperedArgs = @($baseArgs)
$digestIndex = [Array]::IndexOf([object[]]$tamperedArgs, '-ExpectedSha256')
if ($digestIndex -lt 0) {
    [System.IO.File]::WriteAllText($ManifestPath, $manifestOriginalText, $utf8NoBom)
    throw 'The ValidateOnly test arguments do not contain ExpectedSha256.'
}
$tamperedArgs[$digestIndex + 1] = $tamperedDigest
try {
    # The helper intentionally serializes expected validation failures into its
    # durable status record instead of throwing to callers. Assert that contract
    # directly; some unexpected PowerShell/runtime failures may still throw.
    & $HelperPath @tamperedArgs
} finally {
    [System.IO.File]::WriteAllText($ManifestPath, $manifestOriginalText, $utf8NoBom)
}
$status = Get-Content -LiteralPath $statusPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
if ([string]$status.state -ne 'failed' -or [string]$status.message -notmatch '(?i)(sha.?256|digest|hash)') {
    throw "Wrong digest did not fail with an integrity-related status under Windows PowerShell 5.1: state=$($status.state) message=$($status.message)"
}
if ((Get-FileSha256Hex -LiteralPath $runtimePath) -cne $runtimeHashBefore) {
    throw 'Wrong-digest validate-only changed the installed runtime executable.'
}
if ((Get-FileSha256Hex -LiteralPath $configPath) -cne $configHashBefore) {
    throw 'Wrong-digest validate-only changed user configuration.'
}

Write-Host 'Windows PowerShell 5.1 update ValidateOnly smoke passed: valid digest accepted; invalid digest rejected; runtime and user configuration unchanged.' -ForegroundColor Green
