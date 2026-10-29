# Changelog


This project follows a lightweight Keep a Changelog structure. GitHub Release notes are generated from merged commits when a version tag is published; update this file with user-visible changes during each development cycle.

## Unreleased

- Normalize and explicitly quote the Windows installer smoke-test destination passed to Inno Setup; report the actual installed directory contents if the desktop host is not found.
- Fix the Windows desktop shell compilation by using a supported WinForms `FontStyle` value for the startup window heading.
- Add a per-user Windows setup installer with a self-contained WinForms/WebView2 desktop host, Start menu entry, optional desktop shortcut, and notification-area controls for reopening, checking updates, and gracefully stopping the runtime.
- Keep the installer separate from the portable ZIP; installed desktop builds use the full setup package for updates until atomic shell/runtime updates and rollback are implemented.
- Gate the installer asset on the same four-platform CI pipeline and verify the signed Microsoft WebView2 bootstrapper, silent installation, installed payload, version check, and uninstall behavior before release.
- Update the public download cards to prefer the Windows setup installer only when that asset exists; the live Releases page is authoritative for current downloadable assets.
- Keep user settings, downloaded engines, models, WebView2 profile, and logs outside the application directory and preserve them by default on uninstall.
- Fix the Windows PowerShell 5.1 publisher regression smoke test so the expected non-zero exit from its fake Git command does not leak into the GitHub Actions step result after every assertion passes.
- Reorder the product roadmap around an actual installed desktop app: Windows installer/app shell and predictable process lifecycle first, then local hardware detection and evidence-backed engine/model recommendations, then expansion of engine families and cross-platform installer parity.
- Make the website and README explicit that `v0.1.0-alpha` is a portable archive/browser-dashboard preview, not yet a conventional installed desktop application.

- Fix the stable publisher and release-gate scripts for Windows PowerShell 5.1: successful Git commands that report diagnostics on stderr no longer abort the publishing flow; actual Git exit codes remain enforced, and missing optional Git identity settings are handled safely.
- Add a Windows PowerShell 5.1 CI regression test for successful native Git stderr output, non-zero exit-code failures, and optional missing Git identity settings.
- Add one stable maintainer publishing command that imports the prepared Git bundle with its original commit SHA, pushes `main` without force, waits for the four-platform CI and Pages job, then auto-increments the alpha release tag and waits for release CI.
- Add platform-native root launchers and a three-step first-run path on the project site; normal users start the packaged app and verify local inference by chatting in the dashboard rather than running developer test scripts.
- Add a manual dashboard update check, visible update status, and automatic metadata refresh on launch/focus/periodically; make non-applicable update/install limitations explicit.
- Document the one-time manual migration from the first `v0.1.0-alpha` package to the new launcher-enabled package; preserve settings, engines, models and logs in the user-data folder.
- Add a Windows PowerShell real-engine smoke test in the source tools for the configured `llama-server.exe` and GGUF model. It validates the GGUF header, waits for engine readiness, checks `/v1/models`, sends a non-streaming chat request, records a local report without generated response text, and cleans up only processes it started.
- Document the Windows real-engine validation process and update the roadmap to reflect the successful `v0.1.0-alpha` release and the remaining device-level CPU/CUDA checks.
- Fix Windows updater parameter validation so `-ValidateOnly` can verify a staged package without a running runtime PID; actual apply operations still require a positive runtime PID. This unblocks the tampered-archive SHA-256 check in the Windows package smoke test.
- Add a user-confirmed Windows portable-release update apply flow: revalidate the staged SHA-256, reject unsafe ZIP paths/symlinks/oversized archives, check installation-folder write access before shutdown, prepare a same-volume copy and rollback backup, gracefully stop/restart the runtime, and restore the previous application files if the new version fails `/health`.
- Persist per-file SHA-256 values in the rollback manifest and recover interrupted Windows updates at the next launcher start; recovery validates the backup before restoring, records durable progress states, finalizes an update if its target runtime is already healthy, and retries recovery after an earlier rollback failure.
- Keep update application disabled in `-dev` previews and stage-only on Linux/macOS; the dashboard exposes apply only when the current tagged Windows package contains the update helper.
- Add staged-update status revalidation and an explicit Apply update & restart action with a confirmation dialog; user configuration, engines and model weights remain outside the replaced app directory.
- Add a Windows CI update-helper smoke test that rejects a modified archive, validates the packaged archive, updates a temporary installation, checks `/health`, confirms user-data preservation, and injects a post-replacement failure to verify automatic rollback/restart.
- Extend the Windows update smoke test to forcibly terminate the detached helper after replacement and to inject a rollback failure, then verify the next launcher restores the exact prior app files and starts a healthy runtime without touching user data.
- Add HTTP smoke coverage for staged-update status, apply capability gates, platform asset metadata, and fail-closed update URL/digest validation.
- Add per-platform CI performance summaries for dependency install/configure, build, tests, package smoke checks and runner-side elapsed time; include Windows vcpkg cache hit identity, matched key, file counts and cache sizes to distinguish cache misses from runner queue delays.

- Refresh the published-release website view: show download links for Windows, Linux, macOS Intel and Apple Silicon assets, update the displayed product version to `0.1.0-alpha`, and remove stale copy saying the first version tag has not been published.

- Consolidate Windows, Linux, macOS Intel and macOS Apple Silicon build/test/package jobs into the canonical workflow. Main pushes create four preview artifacts; Pages deploys only when all four pass; `v*` releases publish all four packages and a single SHA-256 manifest. The weekly schedule runs only the macOS matrix.
- Bundle Homebrew dylib dependencies in relocatable macOS preview/release packages and smoke-test the extracted archives on their native architectures. Packages remain ad-hoc signed and are not notarized.
- Cache Windows vcpkg binary packages and verified source downloads using keys tied to the vcpkg revision, MSVC compiler version, and dependency manifest. Save the dependency cache immediately after successful configure so a later compile/test failure does not throw away a 16-minute cache seed. Runtime source is still rebuilt and tested on every run; vcpkg ABI validation prevents incompatible dependency binaries from being reused.
- Capture the Windows CMake/vcpkg configure log as a short-lived artifact on failure, making the specific failing package/error available for diagnosis.
- Fix Windows duplicate-runtime detection by opening the listening socket with address reuse disabled before binding; a second process now fails promptly instead of appearing to hang during the HTTP smoke test.

- Fix Windows restart recovery: use the configured runtime port, reuse a healthy existing instance, and identify the PID/path of a listener that does not answer `/health`; prompt before stopping only an unresponsive Local AI Runtime process.
- Log startup attempts and startup failures before opening the listening socket, so port conflicts can be diagnosed even when the dashboard cannot load. Print the “listening” message only after the bind succeeds.
- Add a loopback-only graceful shutdown endpoint so the launcher can stop an older runtime and its managed engine cleanly before switching to a different extracted copy.
- Restrict CORS origins and disable credentialed CORS for the managed llama.cpp server; migrate existing first-party managed engine configs at load time without rewriting user config files.
- Test duplicate-instance port conflicts and verify that startup-failure events are included in local diagnostics.
- Improve the dashboard chat smoke test with a concise same-language system instruction and conservative sampling/token limits for the starter model.
- Record local-only chat request metadata (timing, HTTP status, character counts, finish reason, and available token usage) without logging prompts or model responses.
- Clarify that small starter-model answer quality can vary and provide a simple baseline test prompt.
- Fix llama.cpp package discovery: inspect recent official releases, prefer the newest stable binary when one is published, and fall back to a digest-verified official prerelease only when required for the current platform/backend. Display the selected upstream tag in download progress.
- Organize the component library with search, type filters, more visible license acknowledgement controls, and in-page links to the library, installed engines, and chat test. Collapse the advanced manual engine setup by default.
- Use PowerShell `Invoke-WebRequest -UseBasicParsing` in the launcher health check to avoid the interactive script-parsing warning.
- Fix Windows GGUF finalization by closing the header-check file before rename; preserve complete, SHA-256-verified staging models after transient finalization errors and reuse them on retry.
- Add local rotating runtime diagnostics, download-stage/error events, UI error capture, and a user-initiated diagnostic report export with path redaction. Optional engine logs are included only when the user selects the opt-in checkbox; no telemetry is sent.
- Store bounded engine process logs under the per-user Local AI Runtime data directory and distinguish catalog refresh from download-job status refresh in the dashboard.
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
- Build Windows/Linux preview archives automatically on each `main` push, smoke-test extracted packages, and upload commit-specific artifacts with 14-day retention.
- Expose the preview workflow from the website before the first public release exists.
- Fix Windows SHA-256 stack overflow risk by moving the 1 MiB hashing buffer to heap memory; add core-test stage diagnostics.
- Keep fast per-push CI required for Windows and Linux packages; move experimental macOS compatibility to a weekly/manual workflow with a time limit and no Homebrew auto-update.
- Unify pull-request validation, Windows/Linux preview packaging, tag-driven Releases, and final GitHub Pages publication in one canonical pipeline.
- Publish the site only after both Windows and Linux package smoke tests succeed on `main`.

### Added
- Startup update check in the local dashboard against published stable GitHub Releases; updates are not silently installed.
- Public project site with live release downloads and a version-controlled feature manifest.
- Tag-driven Windows x64 and Linux x64 release packaging with SHA-256 checksums.
- macOS compile/test CI in addition to Windows and Linux CI.
- A Windows Winsock header-order fix for MSVC/Boost.Asio compilation.


### Security
- Keep the runtime API bound to loopback by default.
- Release binaries are never replaced silently. Tagged portable Windows releases require explicit confirmation and perform a backup plus health-checked rollback; development builds and Linux/macOS remain stage-only. SHA-256 integrity checks are not a substitute for signed release provenance.
