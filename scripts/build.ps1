[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build',
    [switch]$Clean,
    [switch]$NoTests
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $repoRoot
try {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'CMake was not found. Install CMake 3.24+ and reopen PowerShell.'
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw 'Git was not found. Install Git for Windows; it is needed to bootstrap vcpkg on first run.'
    }

    $vcpkgRoot = $null
    foreach ($candidate in @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path (Join-Path $candidate 'scripts/buildsystems/vcpkg.cmake'))) {
            $vcpkgRoot = (Resolve-Path $candidate).Path
            break
        }
    }

    if (-not $vcpkgRoot) {
        $vcpkgRoot = Join-Path $repoRoot '.tools/vcpkg'
        $toolchain = Join-Path $vcpkgRoot 'scripts/buildsystems/vcpkg.cmake'
        if (-not (Test-Path $toolchain)) {
            if (Test-Path $vcpkgRoot) {
                throw "Found '$vcpkgRoot' but it is not a valid vcpkg checkout. Remove that directory and retry."
            }
            Write-Host 'First run: downloading vcpkg into .tools/vcpkg. An internet connection is required.' -ForegroundColor Cyan
            New-Item -ItemType Directory -Force -Path (Split-Path $vcpkgRoot -Parent) | Out-Null
            & git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkgRoot
            if ($LASTEXITCODE -ne 0) { throw 'Could not clone vcpkg. Check your network/proxy and retry.' }
        }
        $bootstrap = Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat'
        if (-not (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
            Write-Host 'Bootstrapping vcpkg...' -ForegroundColor Cyan
            & $bootstrap -disableMetrics
            if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
        }
    }

    $env:VCPKG_ROOT = $vcpkgRoot
    $toolchain = Join-Path $vcpkgRoot 'scripts/buildsystems/vcpkg.cmake'
    if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found: $toolchain" }

    if ($Clean -and (Test-Path $BuildDirectory)) {
        Remove-Item -Recurse -Force $BuildDirectory
    } elseif (Test-Path (Join-Path $BuildDirectory 'CMakeCache.txt')) {
        # CMake cannot reliably switch an existing build tree from a plain
        # configure to vcpkg/toolchain mode. Detect this common first-run case
        # (including the earlier missing-Boost configure) and reset only the
        # generated build directory before continuing.
        $cacheMatch = Select-String -Path (Join-Path $BuildDirectory 'CMakeCache.txt') -Pattern '^CMAKE_TOOLCHAIN_FILE:[^=]+=(.*)$' -List
        $cachedToolchain = if ($cacheMatch) { $cacheMatch.Matches[0].Groups[1].Value.Trim() } else { '' }
        $currentToolchain = [System.IO.Path]::GetFullPath($toolchain)
        $cacheToolchain = if (-not [string]::IsNullOrWhiteSpace($cachedToolchain)) { [System.IO.Path]::GetFullPath($cachedToolchain) } else { '' }
        if ([string]::IsNullOrWhiteSpace($cacheToolchain) -or $cacheToolchain -ne $currentToolchain) {
            Write-Warning 'The existing build directory was configured without this vcpkg toolchain. Clearing generated build output and reconfiguring.'
            Remove-Item -Recurse -Force $BuildDirectory
        }
    }

    $tests = if ($NoTests) { 'OFF' } else { 'ON' }
    Write-Host "Configuring Local AI Runtime ($Configuration)..." -ForegroundColor Cyan
    & cmake -S . -B $BuildDirectory "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DVCPKG_MANIFEST_MODE=ON "-DLAR_BUILD_TESTS=$tests"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed. See the first error above.' }

    Write-Host 'Building...' -ForegroundColor Cyan
    & cmake --build $BuildDirectory --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. See the compiler error above.' }

    if (-not $NoTests) {
        $ctestFile = Join-Path $BuildDirectory 'CTestTestfile.cmake'
        if (-not (Test-Path $ctestFile)) {
            throw 'CTest did not generate CTestTestfile.cmake; tests were not configured. Re-run with -Clean.'
        }
        Write-Host 'Running tests...' -ForegroundColor Cyan
        & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
    }

    Write-Host "Build completed. Executable: $(Join-Path $BuildDirectory ($Configuration + '/local-ai-runtime.exe'))" -ForegroundColor Green
}
finally {
    Pop-Location
}
