# Contributing

Thanks for helping improve Local AI Runtime. The project is experimental; discuss major architectural changes in an issue before investing in a large pull request.

## Before opening a pull request

1. Keep changes focused and explain the user problem they solve.
2. Add or update tests for behavior changes.
3. On Windows, run `.\scripts\build.ps1`; on Ubuntu/Debian, install the documented dependencies then run `./scripts/build.sh`. Alternatively build with CMake and run `ctest --test-dir build --output-on-failure`.
4. Do not commit model weights, API keys, personal local paths, logs, build output, vcpkg downloads, or downloaded binaries.
5. Do not claim GPU/model compatibility unless the exact combination was tested and recorded.
6. Prefer explicit argument arrays; never introduce shell command concatenation for process startup.

## Style

- C++20; keep public interfaces small and documented.
- Avoid unnecessary runtime dependencies.
- Preserve loopback-only behavior unless a reviewed security design changes it.
- Include platform-specific tests where process behavior differs between Windows and POSIX.
