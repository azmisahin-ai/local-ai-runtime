$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('lar-publisher-git-smoke-' + [guid]::NewGuid().ToString('N'))
$originalPath = $env:PATH
$originalFakeExit = $env:LAR_FAKE_GIT_EXIT

try {
    New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
    $fakeGitPath = Join-Path $temporaryRoot 'git.cmd'
    @'
@echo off
if "%LAR_FAKE_GIT_EXIT%"=="0" (
  echo synthetic bundle is okay 1>&2
  exit /b 0
)
echo synthetic git failure 1>&2
exit /b 17
'@ | Set-Content -LiteralPath $fakeGitPath -Encoding ASCII

    # Put the fake executable first so the extracted publisher helper can be
    # tested without network access, a real repository, or modifying user files.
    $env:PATH = $temporaryRoot + ';' + $originalPath
    $env:LAR_FAKE_GIT_EXIT = '0'

    foreach ($relativePath in @('Publish-LocalAIRuntime.ps1', 'scripts/Publish-LocalAIRuntime.ps1')) {
        $scriptPath = Join-Path $repositoryRoot $relativePath
        $tokens = $null
        $parseErrors = $null
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
        if ($parseErrors.Count -gt 0) {
            throw "PowerShell syntax is invalid in ${relativePath}: $($parseErrors -join '; ')"
        }

        $functionAst = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-Git' }, $true)
        if ($null -eq $functionAst) { throw "Invoke-Git was not found in ${relativePath}." }
        Invoke-Expression $functionAst.Extent.Text

        $successOutput = Invoke-Git -GitArgs @('bundle', 'verify', 'synthetic.bundle')
        if ($successOutput -notmatch 'synthetic bundle is okay') {
            throw "${relativePath}: successful Git stderr output was not preserved. Output: $successOutput"
        }

        $env:LAR_FAKE_GIT_EXIT = '17'
        $caughtExpectedFailure = $false
        try {
            Invoke-Git -GitArgs @('status', '--porcelain') | Out-Null
        } catch {
            $caughtExpectedFailure = $true
            if ($_.Exception.Message -notmatch 'exit code 17' -or $_.Exception.Message -notmatch 'synthetic git failure') {
                throw "${relativePath}: failed Git command did not provide its exit code and diagnostic. $($_.Exception.Message)"
            }
        }
        if (-not $caughtExpectedFailure) { throw "${relativePath}: non-zero Git exit code was not rejected." }

        $env:LAR_FAKE_GIT_EXIT = '17'
        $optionalOutput = Invoke-Git -GitArgs @('config', '--get', 'user.name') -AllowNonZeroExit
        if (-not [string]::IsNullOrEmpty($optionalOutput)) {
            throw "${relativePath}: optional non-zero Git command should return an empty string."
        }

        Write-Host "Publisher Git stderr smoke passed: $relativePath" -ForegroundColor Green
        Remove-Item Function:\Invoke-Git -ErrorAction SilentlyContinue
        $env:LAR_FAKE_GIT_EXIT = '0'
    }
} finally {
    $env:PATH = $originalPath
    if ($null -eq $originalFakeExit) { Remove-Item Env:\LAR_FAKE_GIT_EXIT -ErrorAction SilentlyContinue } else { $env:LAR_FAKE_GIT_EXIT = $originalFakeExit }
    if (Test-Path -LiteralPath $temporaryRoot) { Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
