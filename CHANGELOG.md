# Changelog

This project follows a lightweight Keep a Changelog structure. GitHub Release notes are generated from merged commits when a version tag is published; update this file with user-visible changes during each development cycle.

## Unreleased

- Add a managed component catalog in the local dashboard for verified llama.cpp CPU/CUDA release archives and a pinned Qwen2.5 1.5B Q4_K_M GGUF model, with explicit license consent, progress/cancel jobs, HTTPS-only transfer, size limits, SHA-256 verification, GGUF header validation and safe archive extraction.
- Install engine/model files in a per-user data directory and register the first llama.cpp engine with the runtime configuration.
- Align macOS's launcher data root with the downloader's Application Support path, and preserve an explicit XDG data-root override.
- Rebuild catalog cards after failed/canceled downloads so a user can retry without refreshing the whole page manually.
- Validate the Linux portable package's dynamic dependencies before uploading preview/release artifacts.
- Bound completed download job history, reap finished worker threads, verify an existing model before reporting it as installed, and remove newly activated engine files if config registration fails.
- Bundle Linux runtime shared-library dependencies and preserve SONAME aliases in portable release packages; the release workflow checks extracted package startup via `/health`.

- Compile all MSVC test targets with `/utf-8` so Unicode-path test literals are interpreted consistently with the UTF-8 config/process contract.
- Run hosted macOS CI on the explicit `macos-15-intel` label while the standard ARM64 pool is reporting runner-allocation capacity failures.

- Add a dashboard form to configure an already installed `llama-server` and local `.gguf` model, persist the configuration safely, and apply it while all managed engines are stopped.
- Add live engine readiness checks using `/health` with a `/v1/models` fallback; distinguish readiness from process-running state and keep chat disabled until ready.
- Add setup endpoint tests for invalid model paths, configuration persistence, and refusing changes while an engine is running.

- Validate extracted Windows and Linux release archives by launching the packaged runtime and checking `/health` before publishing a GitHub Release.
- Add a manual, non-publishing release rehearsal that uploads installable packages as workflow artifacts before a version tag is created.

- Replace the POSIX post-fork C++ child setup with `posix_spawn` file actions and a process-group attribute. This avoids calling non-async-signal-safe C++/filesystem code after `fork()` from a multithreaded HTTP server, a plausible cause of the macOS child-start hang.
- Replace the Python child-process fixture with a native C++/Boost.Beast fake HTTP engine, removing Python-launcher/environment differences from the process-manager smoke test.
- Link the native fake HTTP engine against Winsock on Windows and use header-only Boost.System consistently.
- Keep explicit startup/readiness assertions and process/log diagnostics when the fake engine cannot be reached.
- Make the HTTP smoke test allocate distinct runtime/upstream ports and wait for the fake engine API itself to be ready before testing proxying.
- Make Windows and Unix developer launchers find binaries produced by the default CMake build directories as well as release-package binaries.
- Add CI smoke tests for the actual developer launcher scripts on Windows, Linux, and macOS.


### Added
- Startup update check in the local dashboard against published stable GitHub Releases; updates are not silently installed.
- Public project site with live release downloads and a version-controlled feature manifest.
- Tag-driven Windows x64 and Linux x64 release packaging with SHA-256 checksums.
- macOS compile/test CI in addition to Windows and Linux CI.
- A Windows Winsock header-order fix for MSVC/Boost.Asio compilation.

### Security
- Keep the runtime API bound to loopback by default.
- Release binaries are not self-replaced automatically in this alpha; manual review is required before installing a package.

## Unreleased

- Build Windows/Linux preview archives automatically on each `main` push, smoke-test extracted packages, and upload commit-specific artifacts with 14-day retention.
- Expose the preview workflow from the website before the first public release exists.
