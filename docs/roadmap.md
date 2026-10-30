# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Distribution status

- The `v0.1.2-alpha` milestone introduced the Windows per-user setup installer `local-ai-runtime-windows-x64-setup.exe`; the current published baseline at this roadmap revision is `v0.1.17-alpha`. This baseline includes the user-confirmed full-installer in-app update path with whole-install backup, restart/health verification and interruption recovery. The user has confirmed a clean installation, launch, and successful Qwen2.5 starter-model chat on one Windows environment. This is not evidence for both Windows 10 and 11, GPU inference, or every upgrade/uninstall path. Publisher signing remains outstanding. Always check the [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) for the current assets.
- The new Windows shell hosts the existing dashboard in WebView2, starts the C++ runtime without a console window, and uses a tray menu for reopen, update check, and explicit graceful exit. The installer/payload passed tagged Windows CI; the site may offer it as an alpha, but it must not be described as a mature general-availability product until clean Windows 10/11 manual acceptance is complete.
- The managed catalogue currently starts with official `llama.cpp` CPU/CUDA packages and one pinned Qwen2.5 1.5B GGUF model. The catalog pairs the CUDA server archive with its official companion runtime/DLL archive, verifies both, and records their hashes; physical GPU inference remains unverified until a real hardware run. The current manifest candidate moves component display metadata and the install allowlist into `catalog/manifest-v1.json`, embedded into the runtime at build time; publisher CI must pass before it is treated as released. It is not yet a catalogue of every relevant C/C++ engine or model.
- The dashboard now has five user-facing destinations (Overview, Chat, Models, Activity, Settings), a dedicated chat workspace, English/Turkish locale resources, dynamic Activity/job and engine-state labels, localized update guidance, and a dismissible four-step first-run guide. Keyboard skip navigation, focus targets, visible focus, live status announcements, and reduced-motion handling are included. Server-supplied error details, upstream component names, capability metadata and license text remain source-provided by design. See [product information architecture](product-information-architecture.md) and [UI/UX principles](ui-ux.md).
- Local hardware inventory and first-pass heuristic recommendations are implemented. Evidence-backed per-device live inference measurements and validated compatibility profiles remain in progress.
- Hosted CI uses a fake OpenAI-compatible engine and intentionally does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference therefore requires a separate maintainer validation run.
- Windows tagged portable packages support user-confirmed update apply/restart with checksum-verified backup and rollback. The installed desktop updater shipped in `v0.1.11-alpha`: it downloads the official full setup asset, verifies the SHA-256 digest, backs up the complete install folder, applies the setup package, starts the new shell/runtime and requires a matching `/health` response; failures and interrupted installs restore the verified backup. Its Windows CI smoke is part of the main/tagged pipeline; clean Windows 10/11 manual acceptance beyond the user-confirmed first-run on one Windows environment remains a release gate. Linux/macOS currently verify and stage an update but require manual archive replacement.

The website can offer the Windows setup executable as an alpha because its tagged CI passes install/payload/uninstall checks; the portable ZIP remains a fallback. Clean Windows 10/11 user-session acceptance is still required before calling the desktop experience mature.

## Current foundation status — 2026-10-30

The published reference point is `v0.1.17-alpha` (`976dda1`). Both main and tag CI passed on Windows x64, Linux x64, macOS x64 and macOS arm64. The user has performed a clean installation of an earlier release on one Windows environment, launched the app, installed/used the Qwen2.5 1.5B starter model, and received a local chat response. Do not ask them to repeat this first-run acceptance. The test does not establish Windows 10 and Windows 11 coverage separately, uninstall preservation, a desktop in-app upgrade from every prior release, or real RTX 3060 GPU inference.

The first UI foundation slice is published in `v0.1.12-alpha`: five-area navigation (Overview, Chat, Models, Activity, Settings), task-first Overview, dedicated Chat, English/Turkish resources, and `GET /v1/activity`. `v0.1.13-alpha` adds SSE streaming, client cancellation, and bounded local conversation history. `v0.1.14-alpha` adds persistent component-job records and interrupted-state recovery while preserving `/v1/downloads` compatibility. The phase-4 implementation extends the same `jobs-v1` contract and Activity view to engine start/stop and inference, including explicit cancellation for active streamed inference; it was published in `v0.1.15-alpha` after all platform/tag CI passed. Phase 5 adds live system and managed-engine resource telemetry to Activity and was published in `v0.1.16-alpha` after the four-target main/tag CI passed. Phase 6 completes the first-run guide, broader EN/TR dynamic labels, error-detail framing and keyboard/reduced-motion accessibility; it was published in `v0.1.17-alpha` after the four-target main/tag CI passed. The current phase-7 candidate adds the versioned component manifest, drives both API metadata and install allowlisting from it, and embeds it into the runtime; it still requires the normal publisher CI gates before release. An append-only event ledger remains future work.

The next work remains foundation-only: improve the end-user UI before adding another engine family; complete the Activity/job contract and localization; establish the checked-in manifest as the source for the existing reviewed catalog; define and test the common engine adapter lifecycle; strengthen hardware evidence and distribution trust; then reach cross-platform parity. Qwen3.5 and Gemma 4 can be evaluated as model-family pilots on the existing `llama.cpp` engine after the manifest and compatibility work, without treating them as new engine families.

## Priority 0 — keep the release pipeline green

### Foundation delivery sequence (no new engine family until gates below are complete)

1. [x] Five-area task-first UI shell and first EN/TR resources (`v0.1.12-alpha`).
2. [x] Incremental chat streaming, stop/cancel, and bounded device-local conversation history (`v0.1.13-alpha`).
3. [x] Unified Activity/Job lifecycle: durable component and runtime-operation records, interrupted-state recovery, reviewed download retry, explicit streamed-inference cancellation, and four-platform publisher CI acceptance (`v0.1.15-alpha`). The separate append-only event ledger remains future work.
4. [x] Runtime Resource Manager: live system CPU/RAM and managed-engine CPU/RSS, memory-pressure heuristics, and conditional NVIDIA system-wide utilization/VRAM snapshots are published in `v0.1.16-alpha` after four-target main/tag CI. Automatic model unloading, admission control, and non-NVIDIA GPU telemetry remain future work.
5. [~] Localization, accessibility and first-run onboarding: a four-step device/model/engine/chat guide, persisted dismissal/reopen, expanded EN/TR dynamic UI labels, locale-key/placeholder parity checks, keyboard skip navigation, focus targets, live status announcements, and reduced-motion support are published in `v0.1.17-alpha`; manual assistive-technology review remains open.
6. [~] Versioned component authoring manifest as the single source for catalogue display and installation allowlists: the current candidate adds the embedded manifest, strict installer strategies, target parity and API/allowlist regression tests; four-platform publisher CI is pending.
7. [ ] Shared engine adapter lifecycle and dependency resolver.
8. [ ] Hardware compatibility evidence and real-task profiles, including the user-confirmed RTX 3060 laptop profile.
9. [ ] Distribution trust: signing, provenance and verified catalogue updates.
10. [ ] Cross-platform installer/update/recovery and UX parity, followed by first modality/engine-family pilots.


- [x] Fix the Windows PowerShell 5.1 publisher smoke-test regression and confirm the canonical four-target pipeline passes. `f841c80` was the confirmed green publisher/installer baseline when this roadmap update began (`v0.1.2-alpha`).
- [x] Keep one maintainer command and one stable bundle filename (`local-ai-runtime-update.bundle`). A push uses a normal fast-forward, never force-pushes, and stops at a failed CI gate.
- [x] Validate the desktop shell and per-user setup installer on Windows CI, including silent install, payload verification and uninstall. `v0.1.2-alpha` is published from the first green installer pipeline.
- [x] Keep publisher/installer tests green on every main push. A failed main build must never create a release tag; the publication script enforces this with separate main and tagged CI gates.

## Priority 1 — real Windows desktop installation (first product milestone)

- [x] Build the self-contained WebView2/WinForms desktop host and Inno Setup per-user installer; Windows CI packages, installs, verifies and uninstalls it, including the Start menu entry and optional desktop shortcut.
- [x] User-confirmed clean installation, launch, and first successful Qwen2.5 1.5B starter-model chat on one Windows environment.
- [ ] Complete remaining clean Windows 10/11 manual acceptance for both supported OS versions, WebView2 availability, user-session lifecycle, upgrade, rollback and uninstall before calling the installed app mature. Do not ask the user to repeat the already-completed first-run test.
- [ ] Validate console-free launcher invocation, single-instance activation, tray reopen/update actions, close-to-tray behavior, explicit graceful exit, and isolated user-data storage in Windows CI/manual acceptance.
- [~] Add a clear first-run guide with device/model/engine/chat progress, dismiss/reopen, and focus-aware navigation. Full guided installs with recommendation-specific repair/retry remain open; a user-initiated local diagnostics export already exists. Do not ask users to run developer tests.
- [x] Implement a user-confirmed installed-desktop update hand-off: download the full official setup installer, verify GitHub SHA-256/size/asset identity, stop the desktop/runtime gracefully, make a per-file SHA-256 verified full-install backup, run Inno Setup against the existing per-user install root, verify the new shell/runtime, require matching local health, and roll back on failure. User data stays outside the install root. Windows CI fault-injection covers setup failure and process interruption; the new build must pass tagged CI and clean Windows 10/11 acceptance before this is considered complete for users.
- [x] Keep the installed desktop updater's newest status record authoritative per installation, including terminal states, so an old interrupted update cannot override a newer successful update. Windows CI tests interrupted setup recovery and whole-install rollback; further stale-status and rollback-failure acceptance cases remain part of manual validation.
- [x] Run updater validation under inbox Windows PowerShell 5.1 as well as PowerShell 7; the portable validate-only regression and desktop-update helper are exercised by the Windows CI workflow. Keep the PowerShell 5.1 path covered on every main/tagged pipeline.
- [x] Make the installer/uninstaller keep mutable user data outside the application directory and preserve it by default.
- [ ] Add a separate explicit option to remove user data during uninstall and verify it safely.
- [ ] Validate install, launch, close/reopen, upgrade, forced interruption recovery, rollback, and uninstall on clean Windows 10/11 x64 virtual machines. Verify that user data survives an application update.
- [ ] Use `/v1/system/identity` and a persistent per-user `runtime-instance.id` to verify that the desktop shell, launcher, runtime diagnostics, catalogue, and updater all point at the same user-data root; exercise this contract during Windows install/update/recovery CI and clean Windows 10/11 acceptance.

**Windows milestone acceptance:** a new user downloads one installer from the project site, installs it, launches from Start, sees a desktop window without a console, installs a suitable CPU engine/model through the UI, produces a real chat response, and updates from the UI without manually moving ZIP files or editing JSON.


## Priority 1B — release trust and localized UX

The initial UX shell and the four-step first-run guide are implemented and published in `v0.1.17-alpha`. The phase-6 slice expands dynamic EN/TR localization across job/engine/update/model labels, adds a keyboard skip link, translated accessible names, focus-aware onboarding navigation, status announcements and reduced-motion support. A fully guided install/repair wizard, native screen-reader review, and broader clean-OS acceptance remain required before calling the UX milestone complete.

- [ ] Decide the Windows distribution trust model. For GitHub-hosted setup downloads, acquire a legitimate Authenticode signing identity, keep signing material in protected CI secrets, sign the installer and app executables, verify signatures/timestamps in CI, and continue publishing SHA-256 checksums. Do not claim this guarantees immediate SmartScreen reputation; unsigned alpha builds remain clearly labelled until signing is actually configured.
- [~] Establish UI localization before adding more workspaces: English/Turkish resource files, persisted preference, five-area navigation, dynamic Activity/job and engine state labels, catalog/hardware guidance, update progress and user-authored error framing are localized. CI checks matching key sets, placeholder parity and HTML/JS key references. Raw upstream engine/catalog metadata, license text and API-provided diagnostic details remain unchanged; manual native screen-reader testing remains open.
- [~] Define a single Activity model for download, verify, install, engine start/stop, inference and update. `GET /v1/activity` and `activity-v1` now expose a validated snapshot of component/runtime-operation jobs plus engine state; `GET /v1/jobs` shares the `jobs-v1` contract. A separate append-only event history, token-level inference progress, and cancellation for non-streaming engine lifecycle calls remain open.

## Priority 2 — hardware-aware first run and compatibility reporting

- [x] Add the first local hardware inventory endpoint and dashboard panel for OS/architecture, logical CPU count, total/available RAM and available disk; enumerate GPU adapters using DXGI on Windows and DRM/sysfs on Linux.
- [ ] Complete hardware discovery on macOS and add CPU-feature, driver-version, GPU-runtime and VRAM probes where platform APIs expose reliable information.
- [x] Distinguish the installed engine package and configured execution target from actual GPU inference evidence. Current-session logs are parsed for CUDA initialization and explicit offloaded-layer counts, without treating startup messages as live utilization measurements.
- [x] Add an opt-in GPU observation action that sends a fixed local chat request and samples NVIDIA system-wide utilization/memory during the request when `nvidia-smi` is available. It reports sample count, peak utilization and memory before/peak/after; it explicitly warns that system-wide readings may include other apps and does not by itself prove which process used the GPU (current alpha implementation).
- [ ] Validate real GPU offload on a physical NVIDIA device and record driver, model, context, layer offload, telemetry sample and repeatability evidence.
- [x] Add the first structured, conservative recommendation cards: a pinned CPU chat starter informed by available RAM and disk, plus an NVIDIA CUDA candidate only when a local adapter is detected. Both states explicitly say they are heuristics, not support guarantees.
- [ ] Expose the complete evidence progression `detected`, `available`, `configured`, `installed`, `process-ready`, `task-tested`, and `profile-validated` consistently in the UI and catalogue.
- [ ] Turn the initial RAM-based guidance into measured engine/backend/model recommendations using free RAM, available disk, dedicated/shared GPU memory, driver and known model runtime needs. Offer a CPU fallback when acceleration is unavailable or not verified; never infer backend support from GPU presence alone.
- [ ] Explain estimated download size, model memory needs, context-size tradeoffs, license, and the reason a recommendation is or is not suitable.
- [ ] Record repeatable baseline profiles, including the project's low-hardware target and the referenced RTX 3060 6 GB / 16 GB RAM profile. A profile is a test record, not a universal guarantee.

## Priority 3 — extensible engine and model catalogue

The metadata contract and distribution boundaries live in [engine distribution strategy](engine-distribution.md).

- [x] Separate runtime release, engine packages, and model weights; keep large models out of the runtime installer.
- [x] Starter catalogue: official `llama.cpp` CPU/CUDA assets and a pinned Qwen2.5 1.5B Q4_K_M GGUF model, with license acknowledgement, size limits, HTTPS, SHA-256, safe extraction, progress, cancellation, and user-data installation paths.
- [x] Publish a versioned component-v1 catalog API schema with targets, backend, source/revision, license, dependency resolution, resource estimates, limitations and evidence fields; validate every live `/v1/catalog` response in the HTTP smoke test. See `catalog/schemas/component-v1.schema.json` and [product information architecture](product-information-architecture.md).
- [~] Replace the built-in metadata allowlist with the reviewed, build-time embedded `catalog/manifest-v1.json` source; current candidate drives API metadata, model pins, engine strategy dispatch and platform install targets from it. Four-platform release CI is pending. Arbitrary or remote executable manifests remain prohibited.
- [ ] Run a real `llama-server` + GGUF CPU inference smoke test on Windows and one additional CPU environment; record the exact package/model and local report.
- [x] Implement exact official CUDA server + companion runtime asset selection, SHA-256 verification, safe extraction/merge, aggregate progress, conservative free-space checks, diagnostic asset hashes, and regression coverage for Windows/Linux x64 asset naming. The real GPU task test remains a separate gate.
- [ ] Run CUDA validation with a real NVIDIA GPU, driver, VRAM, model quantization, context and backend evidence. `--n-gpu-layers auto` alone is not proof of GPU inference.
- [ ] Expand model choices with size/quantization and hardware guidance; recommend only combinations with evidence and keep “unverified” combinations clearly labelled.
- [ ] Add engine families incrementally under the adapter contract: consider `stable-diffusion.cpp` for image generation, `whisper.cpp`/`sherpa-onnx` for speech, OpenCV/OCR and embeddings/reranking for knowledge workflows. Evaluate music/SFX and video runtimes separately. Each needs its own licence review, catalog metadata, process/lifecycle tests, platform builds, real task smoke test, and documented hardware requirements before being marked supported.
- [ ] Resume interrupted downloads and probe a newly installed engine/model before marking the combination tested.

## Priority 4 — safe, understandable updates

- [x] A successful `main` push builds/tests all four targets and updates the project website after package gates pass.
- [x] The maintainer publishing flow waits for green `main` CI before creating an alpha tag; the tag workflow then gates GitHub Release publication on the same platform jobs.
- [x] The dashboard checks release metadata at startup, offers a manual check, and verifies matching official release archives by size and SHA-256 before staging.
- [x] Tagged portable Windows releases require explicit user confirmation and have backup, health-check, rollback, and interrupted-update recovery.
- [x] Move the Windows update flow to the installed-app location and provide a user-confirmed “Download & install desktop update” experience with download progress, full-install rollback and startup recovery. Windows CI fault-injects setup failure and helper interruption; clean Windows 10/11 acceptance remains open.
- [ ] Add verified apply/recovery for Linux and macOS before showing one-click installation controls there.
- [ ] Sign the Windows setup executable with a real publisher certificate and timestamp it; configure secure CI signing and key custody. An unsigned alpha may trigger Windows SmartScreen/unknown-publisher warnings; no code change can honestly remove that warning.
- [ ] Plan release provenance/attestation. SHA-256 is an integrity check, not publisher authentication. Do not silently replace executables until trust, recovery, and update threat models are independently tested.

## Priority 5 — cross-platform desktop parity

- [ ] After the Windows installed application and update lifecycle pass clean-machine tests, provide native installation and app lifecycle for Linux and macOS.
- [ ] Keep the same first-run, hardware guidance, engine/model catalogue, update status, and user-data preservation semantics across platforms.
- [ ] Add audio, vision, embeddings, streaming chat, and optional MCP only after the desktop install, engine lifecycle, compatibility evidence and security boundaries remain stable.

## Definition of done for user-facing claims

- “Installer” means a tested installer on a clean supported OS, not just a ZIP archive.
- “Desktop app” means a window users can open/close from the OS, with predictable runtime/process behavior and no required terminal.
- “Supported engine/model/backend” means a repeatable real task test exists for the declared platform/hardware profile.
- “Automatic update” means verified download, explicit or policy-defined user consent, safe apply/restart, rollback, and preserved user data; a release notification alone is not an updater.
