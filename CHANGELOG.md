## Unreleased — multi-model Catalog v2 pinning

- Generalize the compiled `pinned-gguf` allowlist from one Qwen-specific repository to reviewed single-file GGUF files hosted at canonical Hugging Face owner/repository URLs with immutable 40-character revisions.
- Permit multiple non-default GGUF model entries while retaining exactly one explicit default model; verify URL owner/repository, metadata source URL, revision, exact filename, SHA-256, exact bounded byte size and GGUF format agree.
- Reject lookalike hosts, query strings, percent-encoded paths, traversal segments, extra path segments, mutable revisions and arbitrary installer commands.
- Store each model under a component-ID-scoped directory to prevent filename collisions while retaining read compatibility for the existing flat Qwen starter path.
- Add C++ regression checks and a synthetic non-default second-repository manifest fixture; document that multi-file multimodal, ONNX and SafeTensors packages still need dedicated installer strategies.

## Unreleased — explicit Runtime Profile and model activation

- Persist a selected engine/backend component, Chat model component and context size separately from installed artifacts.
- Install CPU/CUDA engine packages side-by-side without silently replacing the active executable; installing or re-verifying a model no longer changes the active Chat model.
- Add `GET/POST /v1/runtime/profile` and a Models-screen profile selector that only applies verified, installed catalogue components while engines are stopped.
- Verify the selected model file size, SHA-256 and GGUF header before activation; keep manual llama.cpp configuration as an Advanced/custom path.
- Block Chat engine start without a selected model, disable Chat Send until the configured engine reports ready, and record unavailable inference attempts in Activity.
- Persist dotted catalogue component IDs safely and add `runtime-profile-v1`, core persistence and HTTP/API regression coverage.
- Keep hardware auto-selection, persistent install queuing and non-llama engine families out of this stage; those require separate compatibility and adapter work.

## Unreleased — runtime profiles and explicit model selection

- Persist the active managed engine component, model component and context size as a Runtime Profile.
- Separate installation from activation: installing CPU/CUDA engine packages side-by-side no longer changes the selected backend; downloading or re-verifying a model no longer replaces the model currently used by Chat.
- Add `GET/POST /v1/runtime/profile`, a `runtime-profile-v1` schema, and a Models-page selector for installed engine package, installed Chat model and context size.
- Apply a managed profile only while engines are stopped; verify installed model size, SHA-256 and GGUF header before selecting it.
- Guard engine start and both streaming/non-streaming Chat APIs when no model is selected or the local engine is not ready; persist unavailable inference attempts in Activity.
- Keep manual llama.cpp configuration as an Advanced/custom setup path, separate from managed catalog profiles.
- Fix component ID persistence so catalog IDs containing dots (for example `llama.cpp-cuda12` and `qwen2.5-1.5b-q4km`) survive config save/restart validation.
- Keep the first-run model step incomplete until an engine and model are actually configured, not merely downloaded.

## Unreleased — task-first Catalog v2 and simplified Models UI

- Split the Models catalogue into distinct model-weight and engine-runtime sections so backend packages are no longer presented as additional language models.
- Add optional structured `model_profile` metadata for task group, architecture, parameter count, format and quantization while preserving the existing `/v1/catalog` response contract and adding no breaking API version change.
- Add metadata-driven task filtering for Chat, Vision, Embeddings, Speech, Image generation, Video and Audio as reviewed catalogue entries become available.
- Replace the large disabled Installed action with a compact installed/selected status and a small action that drafts the component into the Runtime Profile; activation still requires explicit profile application.
- Show queued/installing state with a View Activity action, retain license acknowledgements across catalogue refreshes, and keep details, compatibility, provenance, evidence and local install path behind an expandable details section.
- Add model-profile manifest/schema/API checks and a static Models UI contract test; keep the current single reviewed Qwen starter and three llama.cpp backend entries unchanged.

# Changelog

## Unreleased — cross-platform install queue test stability

- Make the persistent install queue core test platform-neutral by testing cancellation and re-queueing with the reviewed cross-platform Qwen model instead of requesting a CUDA-only component on macOS.
- Poll the test executor cancellation callback with a bounded condition-variable wait so an early assertion cannot strand the DownloadManager destructor while the test-owned condition variable is not notified.
- Keep CUDA 12/13 target coverage in manifest contract tests, where unsupported macOS targets are explicitly asserted, instead of trying to install CUDA on macOS.

## Unreleased — persistent sequential installation queue

- Replace per-install worker threads with one bounded (16 outstanding jobs including the active job), persistent FIFO worker shared by component and runtime-update downloads; keep only one installation pipeline active at a time.
- Make repeated requests for the same queued/active component idempotent, returning the existing job ID instead of producing duplicate installation work.
- Persist queue position and reviewed dependency plans in `jobs-v1`; show queue position, dependency assets, and queued/installing status in Activity and Models without resetting license-checkbox state during periodic refresh.
- Declare matching CUDA 12/13 runtime archives in the embedded component manifest and resolve, SHA-256 verify, extract and merge each archive within its parent llama.cpp engine job.
- Persist unstarted, user-authorized queued work and revalidate its component, platform and license before resuming after restart. Mark active transfers interrupted/canceled without automatic replay.
- Add explicit queued-job cancellation, retry-to-queue behavior, queue capacity limits, duplicate protection, FIFO/one-worker core tests and restart recovery tests.
- Keep `/v1/downloads` backwards compatible and return HTTP 409 for queue capacity/shutdown conflicts.

## Unreleased — manifest-driven component catalog

- Add `catalog/manifest-v1.json` as the versioned source of truth for the reviewed component display metadata and installation allowlist.
- Embed the manifest into the runtime binary at CMake configure time; do not load install policy from a user-writable data folder or a remote source.
- Drive `/v1/catalog` output, component start/retry allowlisting, engine variant dispatch, default-model selection and pinned GGUF URL/filename/size/SHA-256 from the compiled manifest.
- Fail closed on duplicate/invalid IDs, unsupported install strategies or targets, mismatched metadata identity, unsafe model filenames, non-immutable upstream revisions, invalid digests and metadata/install target drift.
- Keep the only v1 installer strategies constrained to official `llama.cpp` release resolution and the reviewed pinned Qwen GGUF model; arbitrary URLs, commands and install scripts remain prohibited.
- Add `manifest-v1.schema.json`, a dependency-free manifest contract test, C++ loader tests and live `/v1/catalog` HTTP schema regression coverage.
- Update catalog authoring and roadmap guidance to distinguish the manifest candidate from a release until four-platform publisher CI passes.

## Unreleased — CI diagnostics and deterministic test-process cleanup

- Extend the bundled PowerShell release gate to list failed GitHub Actions jobs and steps, retrieve check-run annotations, download each failed job log, and print focused error excerpts while preserving the full logs under `Downloads/local-ai-runtime-ci-diagnostics/run-<id>`; public repositories work without a token, while `GH_TOKEN`/`GITHUB_TOKEN` are used when supplied.
- Keep release safety unchanged: failed main CI creates no version tag; failed tagged CI leaves the pushed tag untouched and reports the collected diagnostics.
- Make HTTP smoke-test teardown request graceful engine stop and runtime shutdown before terminating the runtime process; retain the exact fake-engine PID for a targeted last-resort cleanup so Windows does not leave child processes holding temporary log files.
- Prevent temporary-directory cleanup errors on Windows from masking the original HTTP smoke assertion, and verify the runtime confirms child-process termination before reporting a managed engine as stopped.

## Unreleased — localized first-run UX and accessibility

- Add a dismissible first-run guide covering local device discovery, model selection, engine readiness, and the first local chat; derive completion from local runtime state and let users reopen the guide from Settings.
- Add a keyboard skip link to the main landmark, focusable page/section headings for guided navigation, translated accessible names, live progress/status announcements, visible focus states, and reduced-motion support.
- Expand English/Turkish localization across dynamic update guidance/progress, Activity job states/phases, engine state/package/GPU evidence labels, model resource guidance, and user-facing error framing.
- Re-render dynamic panels when the locale changes and keep upstream component names, engine capabilities, license text, and source-provided diagnostic details intact.
- Add CI checks for identical locale keys, matching interpolation placeholders, complete HTML translation attributes and direct JavaScript translation references, plus onboarding/accessibility smoke assertions.
- Update the roadmap and product documentation to reflect the published `v0.1.17-alpha` baseline and distinguish the current manifest candidate from a released milestone.

## Unreleased — live runtime resource manager

- Add `GET /v1/system/resources` with a versioned `resources-v1` snapshot for system CPU/memory pressure, managed engine process CPU/RSS, and available NVIDIA system-wide utilization/VRAM telemetry.
- Add the Activity resource monitor with live local readings and English/Turkish labels; first CPU samples remain unavailable until a second sample establishes a delta.
- Report memory pressure as a transparent heuristic (`normal`, `constrained`, `critical`, `unknown`); do not automatically stop engines, kill processes, or change CPU/GPU backend settings.
- Expose GPU telemetry only when the NVIDIA adapter and `nvidia-smi` provider are both available; explicitly label measurements as system-wide and do not infer per-engine GPU use.
- Keep resource samples transient and local; do not persist prompts, responses, command lines, process paths, or hardware inventory.
- Add process-level resource sampling for managed engine processes on Windows, macOS, and Linux, plus the `resources-v1` schema and HTTP/UI regression coverage.

## Unreleased — unified Activity and runtime-operation jobs

- Forward OpenAI-compatible chat `stream:true` responses as chunked Server-Sent Events so tokens reach the UI as they are generated.
- Detect client disconnects during streamed reads and close the upstream request promptly; keep the same loopback Host/Origin policy for streaming calls.
- Add streamed chat rendering, stop generation, a local conversation list, new-chat and clear-history actions. Store a bounded history in the per-user WebView/browser profile only; do not include prompt or response content in diagnostics.
- Add delayed chunked fake-engine coverage proving that first SSE bytes are delivered before a full generation completes, plus the streaming route security test.
- Improve Activity summary cards so job/engine counters remain clearly separated in the UI.
- Extend the durable `jobs-v1` contract and Activity snapshot to engine start/stop and local inference operations while preserving the legacy `/v1/downloads` routes.
- Persist runtime-operation lifecycle, elapsed duration, and inference output-byte progress; mark unfinished operations `interrupted` on startup without replaying them.
- Allow explicit cancellation of active streamed inference from Activity; keep retry restricted to reviewed component/update workflows and never replay engine lifecycle operations.
- Include unified component and operation job history in exported diagnostics without logging prompt or generated response text.
- Add core, HTTP integration, schema, recovery, and explicit streaming-cancellation regression coverage for the unified Activity path.
- Keep streamed-inference cancellation compatible with the Boost.Asio timer API exposed by Windows and macOS dependency versions; use the non-error-code timer cancellation overload.
- Use the Microsoft secure environment-variable API for the runtime data-root identity on MSVC builds.
- Make the streamed-client-disconnect HTTP smoke test wait for a terminal job state instead of relying on a fixed delay, avoiding scheduler-dependent failures on macOS runners.


## Unreleased

- Replace the developer-oriented single-page dashboard layout with five user-facing areas: Overview, Chat, Models, Activity, and Settings. Preserve the existing local hardware, managed catalogue, runtime, diagnostics, and update workflows behind task-oriented navigation.
- Add first-pass English and Turkish UI resource files with a persisted local language preference; serve only the two reviewed locale resources from the loopback runtime. Remaining dynamic error/catalog/status strings still need complete resource-key extraction.
- Add loopback `GET /v1/activity` with the versioned `activity-v1` snapshot schema, combining current download/update jobs and engine readiness counters for the UI and future local clients. This is a current snapshot, not a durable event timeline.
- Preserve nested dashboard assets (including locale resources) in Linux portable packages and assert both reviewed language files are present in extracted Windows/Linux/macOS package smoke tests.
- Improve the chat entry experience with suggested prompts, keyboard guidance, dedicated conversation space, and a clear local privacy statement. Streaming responses and durable conversation history remain future work.
- Record the user-confirmed clean Windows installation, launch, and first successful Qwen2.5 starter-model chat on one Windows environment. This does not by itself validate both Windows 10 and 11, real NVIDIA GPU inference, or every upgrade/uninstall scenario.

- Fix Windows desktop rollback backup-manifest round-tripping by storing a versioned JSON envelope with an explicit file count and files collection; validate the declared count against the parsed collection and assert the persisted manifest in the rollback smoke test.
- Fix the Windows desktop shell build by disambiguating recovery/update local variables and qualifying shared window-title references from the dashboard form.
- Fix the Windows desktop shell compile failure in the interrupted-update recovery message by escaping the documented user-data backup path correctly in the C# string literal.
- Add user-confirmed in-app updates for installed Windows desktop builds using the full official setup installer, GitHub SHA-256/size verification, detached PowerShell 5.1 hand-off, whole-install backup, post-install runtime/data-root identity verification, automatic rollback and interruption recovery that waits for an active installer to finish.
- Add Windows CI fault-injection coverage for failed setup rollback, forced process interruption recovery, desktop payload validation and preservation of user settings/engine/model data.
- Prevent stale interrupted desktop-update status from overriding a newer terminal result, and verify the restored desktop/runtime starts successfully after rollback.
- Keep the Windows desktop updater unsigned until a legitimate Authenticode identity and protected signing workflow are available; SHA-256 remains an integrity check, not publisher authentication.

- Make only the newest timestamped update status for the current installation authoritative during launcher recovery; stale rollback failures from older tags can no longer trigger an unwanted old restore.
- Add Windows PowerShell 5.1 ValidateOnly regression coverage for valid and invalid SHA-256 digests, plus stopping-phase interruption and stale-status recovery tests.
- Track the remaining Authenticode/SmartScreen trust work, English/Turkish UI localization, and clean Windows 10/11 acceptance as separate product milestones.

- Enforce one explicit per-user data-root contract across the Windows desktop shell, PowerShell launcher, C++ runtime, catalogue/download state and post-update restarts. Persist an opaque runtime-instance ID and expose it through a loopback identity endpoint so the desktop cannot attach to a healthy but stale/different runtime on the configured port.
- Extend HTTP and Windows update smoke tests to prove installed catalogue assets and diagnostic logs are read from the explicit data root and that the root identity survives apply, rollback and interrupted-update recovery.

- Publish a versioned component-v1 schema for catalog metadata and validate real `/v1/catalog` API responses in the HTTP smoke test. Include task/workspace, platform targets, resources, provenance, dependencies, evidence status and lifecycle policy so new engine families can be added consistently.
- Add collapsed “Compatibility & requirements” details to component cards, keeping catalog cards readable while exposing target platforms, prerequisite drivers, memory estimate confidence, dependency packages and validation limits.

- Report session-scoped llama.cpp CUDA initialization and layer-offload evidence in the Engines panel. Previous-session log entries are excluded; a reported offload is clearly distinguished from measured live GPU utilization.
- Add regression tests for positive/zero layer offload, CUDA initialization without a layer count, missing logs, and stale-session evidence.

- Fix Windows installer smoke-test race by waiting for Inno Setup post-uninstall cleanup and preserving the uninstall log on failure.

This project follows a lightweight Keep a Changelog structure. GitHub Release notes are generated from merged commits when a version tag is published; update this file with user-visible changes during each development cycle.

- Add structured local hardware-aware starting recommendations: a conservative CPU/Qwen2.5 1.5B profile uses observed RAM and free disk; NVIDIA CUDA is shown only as a candidate when an adapter is detected, with an explicit not-verified warning. Add HTTP and dashboard smoke coverage for the recommendation schema.
- Add structured hardware recommendation cards with explicit evidence/status labels, and make the Windows launcher reuse its configured port when Winsock confirms it can bind even if the TCP owner table reports an unresolved PID 0/4 entry.

- Add a local-only device inventory panel and `/v1/system/hardware` endpoint with OS/architecture, logical CPU count, total/available RAM, free space for models, and native GPU adapter inventory where supported. Adapter detection is explicitly not a claim that CUDA or any model/backend works.
- Add evidence-aware engine status labels that distinguish the installed package (CPU/CUDA 12/CUDA 13), requested execution target, and unverified real GPU execution.
- Include a bounded `hardware.snapshot` summary in local diagnostics so support reports can show machine resource limits and graphics-adapter identifiers without collecting chat prompts, model responses, or absolute storage paths.
- Add regression coverage for the local hardware endpoint and its privacy/evidence contract; extend the dashboard with refreshable device information and conservative starting guidance based on system memory.


- Normalize and explicitly quote the Windows installer smoke-test destination passed to Inno Setup; report the actual installed directory contents if the desktop host is not found.
- Fix the Windows desktop shell compilation by using a supported WinForms `FontStyle` value for the startup window heading.
- Add a per-user Windows setup installer with a self-contained WinForms/WebView2 desktop host, Start menu entry, optional desktop shortcut, and notification-area controls for reopening, checking updates, and gracefully stopping the runtime.
- Keep the installer separate from the portable ZIP; installed desktop builds use the full setup package and verified whole-install rollback so the shell and runtime remain version-matched.
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
- Separate task workspaces from installable engines/models/dependencies in the dashboard; planned vision, image, speech/audio, music/SFX, video, knowledge/search and automation areas are clearly marked as not yet available.
- Fix llama.cpp CUDA catalog matching: don't mistake `cudart` runtime archives for server binaries; resolve the matching server and official CUDA runtime archive from the same release, verify both SHA-256 digests, safely extract and merge them as one engine install, and show aggregate progress.
- Add catalog regression tests for official Windows/Linux CUDA server/runtime filename pairs and reject mismatched CUDA generations/architectures.
- When Windows reports an occupied runtime port with unresolved PID 0 or kernel-owned PID 4, do not label it System Idle Process or stop a process; persist a verified free replacement port and explain the automatic change to the user.
- Update catalog architecture and roadmap for task workspaces, dependencies, hardware-aware compatibility evidence, real CUDA validation, signed installer requirements, and a later atomic in-app installer update.

- Pair official llama.cpp CUDA server packages with their matching runtime/DLL archives and verify both SHA-256 digests before activation.
- Report both CUDA asset names, sizes and SHA-256 values in local diagnostics; use combined download progress and conservative disk-space estimates.
- Organize the dashboard into task workspaces separate from engine/model catalog items, with planned modalities explicitly labelled as not implemented.
- Improve Windows launcher behavior when a listener is reported as unresolved PID 0/PID 4: select and persist a safe loopback port instead of misleadingly naming System Idle Process or stopping a process.
- Reorganize the roadmap around installed desktop UX, hardware evidence, versioned catalog metadata, safe updates and cross-platform parity.


## Unreleased — opt-in NVIDIA GPU observation

- Add a dashboard action that runs a fixed local chat request and samples NVIDIA system-wide utilization and memory during the request when `nvidia-smi` is available.
- Keep system telemetry distinct from current-session startup/offload evidence and disclose that other applications may contribute to GPU measurements.
- Do not record the fixed prompt or generated response in runtime-owned diagnostics.
- Add CSV parser coverage and an HTTP guard that requires a running CUDA/GPU-configured engine before allowing the observation.
