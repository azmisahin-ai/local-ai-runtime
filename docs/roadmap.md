# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Distribution status

- The `v0.1.2-alpha` milestone introduced the Windows per-user setup installer `local-ai-runtime-windows-x64-setup.exe`; the current published baseline at this roadmap revision is `v0.1.12-alpha`. This baseline includes the user-confirmed full-installer in-app update path with whole-install backup, restart/health verification and interruption recovery. The user has confirmed a clean installation, launch, and successful Qwen2.5 starter-model chat on one Windows environment. This is not evidence for both Windows 10 and 11, GPU inference, or every upgrade/uninstall path. Publisher signing remains outstanding. Always check the [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) for the current assets.
- The new Windows shell hosts the existing dashboard in WebView2, starts the C++ runtime without a console window, and uses a tray menu for reopen, update check, and explicit graceful exit. The installer/payload passed tagged Windows CI; the site may offer it as an alpha, but it must not be described as a mature general-availability product until clean Windows 10/11 manual acceptance is complete.
- The managed catalogue currently starts with official `llama.cpp` CPU/CUDA packages and one pinned Qwen2.5 1.5B GGUF model. The catalog pairs the CUDA server archive with its official companion runtime/DLL archive, verifies both, and records their hashes; physical GPU inference remains unverified until a real hardware run. The current entries are still backed by a reviewed built-in allowlist in `src/downloads.cpp`, not yet by a source-of-truth authoring manifest. It is not yet a catalogue of every relevant C/C++ engine or model.
- The dashboard now has five user-facing destinations (Overview, Chat, Models, Activity, Settings), a dedicated chat workspace, and first-pass English/Turkish locale resources. The primary static labels are localized and the preference persists locally; dynamic catalog, hardware, and error strings still need complete resource-key extraction. See [product information architecture](product-information-architecture.md) and [UI/UX principles](ui-ux.md).
- Local hardware inventory and first-pass heuristic recommendations are implemented. Evidence-backed per-device live inference measurements and validated compatibility profiles remain in progress.
- Hosted CI uses a fake OpenAI-compatible engine and intentionally does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference therefore requires a separate maintainer validation run.
- Windows tagged portable packages support user-confirmed update apply/restart with checksum-verified backup and rollback. The installed desktop updater shipped in `v0.1.11-alpha`: it downloads the official full setup asset, verifies the SHA-256 digest, backs up the complete install folder, applies the setup package, starts the new shell/runtime and requires a matching `/health` response; failures and interrupted installs restore the verified backup. Its Windows CI smoke is part of the main/tagged pipeline; clean Windows 10/11 manual acceptance beyond the user-confirmed first-run on one Windows environment remains a release gate. Linux/macOS currently verify and stage an update but require manual archive replacement.

The website can offer the Windows setup executable as an alpha because its tagged CI passes install/payload/uninstall checks; the portable ZIP remains a fallback. Clean Windows 10/11 user-session acceptance is still required before calling the desktop experience mature.

## Current foundation status — 2026-10-30

The published reference point is `v0.1.12-alpha` (`ed4d7b2`). Both main and tag CI passed. The user has performed a clean installation of the latest release on one Windows environment, launched the app, installed/used the Qwen2.5 1.5B starter model, and received a local chat response. Do not ask them to repeat this first-run acceptance. The test does not establish Windows 10 and Windows 11 coverage separately, uninstall preservation, a desktop in-app upgrade from every prior release, or real RTX 3060 GPU inference.

The first UI foundation slice is published in `v0.1.12-alpha`: five-area navigation (Overview, Chat, Models, Activity, Settings), a task-first Overview, a dedicated chat workspace, English/Turkish static UI resources with a persisted preference, and the loopback `GET /v1/activity` snapshot API. `activity-v1` is explicitly a point-in-time snapshot, not a persistent event history. The next unreleased foundation slice adds actual SSE token streaming, client cancellation, and bounded local conversation history in the Chat workspace; it also corrects the Activity summary layout. Complete localization of dynamic strings, a persistent job/event ledger, and the manifest-driven catalogue remain open work.

The next work remains foundation-only: improve the end-user UI before adding another engine family; complete the Activity/job contract and localization; move reviewed model/engine metadata and install allowlisting to a checked-in versioned authoring manifest; define and test the common engine adapter lifecycle; strengthen hardware evidence and distribution trust; then reach cross-platform parity. Qwen3.5 and Gemma 4 can be evaluated as model-family pilots on the existing `llama.cpp` engine after the manifest and compatibility work, without treating them as new engine families.

## Priority 0 — keep the release pipeline green

### Foundation delivery sequence (no new engine family until gates below are complete)

1. [x] Five-area task-first UI shell and first EN/TR resources (`v0.1.12-alpha`).
2. [ ] Incremental chat streaming, stop/cancel, and bounded device-local conversation history (implemented in the current candidate; awaiting green main and tagged CI).
3. [ ] Persistent Activity/Job lifecycle: durable events, retry/cancel semantics, and unified progress UI.
4. [ ] Full dynamic localization, accessibility and first-run onboarding.
5. [ ] Versioned component authoring manifest as the single source for catalogue display and installation allowlists.
6. [ ] Shared engine adapter lifecycle and dependency resolver.
7. [ ] Hardware compatibility evidence and real-task profiles, including the user-confirmed RTX 3060 laptop profile.
8. [ ] Distribution trust: signing, provenance and verified catalogue updates.
9. [ ] Cross-platform installer/update/recovery and UX parity, followed by first modality/engine-family pilots.


- [x] Fix the Windows PowerShell 5.1 publisher smoke-test regression and confirm the canonical four-target pipeline passes. `f841c80` was the confirmed green publisher/installer baseline when this roadmap update began (`v0.1.2-alpha`).
- [x] Keep one maintainer command and one stable bundle filename (`local-ai-runtime-update.bundle`). A push uses a normal fast-forward, never force-pushes, and stops at a failed CI gate.
- [x] Validate the desktop shell and per-user setup installer on Windows CI, including silent install, payload verification and uninstall. `v0.1.2-alpha` is published from the first green installer pipeline.
- [x] Keep publisher/installer tests green on every main push. A failed main build must never create a release tag; the publication script enforces this with separate main and tagged CI gates.

## Priority 1 — real Windows desktop installation (first product milestone)

- [x] Build the self-contained WebView2/WinForms desktop host and Inno Setup per-user installer; Windows CI packages, installs, verifies and uninstalls it, including the Start menu entry and optional desktop shortcut.
- [x] User-confirmed clean installation, launch, and first successful Qwen2.5 1.5B starter-model chat on one Windows environment.
- [ ] Complete remaining clean Windows 10/11 manual acceptance for both supported OS versions, WebView2 availability, user-session lifecycle, upgrade, rollback and uninstall before calling the installed app mature. Do not ask the user to repeat the already-completed first-run test.
- [ ] Validate console-free launcher invocation, single-instance activation, tray reopen/update actions, close-to-tray behavior, explicit graceful exit, and isolated user-data storage in Windows CI/manual acceptance.
- [ ] Add a clear first-run wizard, progress/error states, repair/retry actions, and a user-readable diagnostics export. Do not ask users to run developer tests.
- [x] Implement a user-confirmed installed-desktop update hand-off: download the full official setup installer, verify GitHub SHA-256/size/asset identity, stop the desktop/runtime gracefully, make a per-file SHA-256 verified full-install backup, run Inno Setup against the existing per-user install root, verify the new shell/runtime, require matching local health, and roll back on failure. User data stays outside the install root. Windows CI fault-injection covers setup failure and process interruption; the new build must pass tagged CI and clean Windows 10/11 acceptance before this is considered complete for users.
- [x] Keep the installed desktop updater's newest status record authoritative per installation, including terminal states, so an old interrupted update cannot override a newer successful update. Windows CI tests interrupted setup recovery and whole-install rollback; further stale-status and rollback-failure acceptance cases remain part of manual validation.
- [x] Run updater validation under inbox Windows PowerShell 5.1 as well as PowerShell 7; the portable validate-only regression and desktop-update helper are exercised by the Windows CI workflow. Keep the PowerShell 5.1 path covered on every main/tagged pipeline.
- [x] Make the installer/uninstaller keep mutable user data outside the application directory and preserve it by default.
- [ ] Add a separate explicit option to remove user data during uninstall and verify it safely.
- [ ] Validate install, launch, close/reopen, upgrade, forced interruption recovery, rollback, and uninstall on clean Windows 10/11 x64 virtual machines. Verify that user data survives an application update.
- [ ] Use `/v1/system/identity` and a persistent per-user `runtime-instance.id` to verify that the desktop shell, launcher, runtime diagnostics, catalogue, and updater all point at the same user-data root; exercise this contract during Windows install/update/recovery CI and clean Windows 10/11 acceptance.

**Windows milestone acceptance:** a new user downloads one installer from the project site, installs it, launches from Start, sees a desktop window without a console, installs a suitable CPU engine/model through the UI, produces a real chat response, and updates from the UI without manually moving ZIP files or editing JSON.


## Priority 1B — release trust and localized UX

The first UX implementation slice has landed in the working branch: Overview, Chat, Models, Activity and Settings navigation; task-first overview; dedicated chat composer with prompt suggestions; and bilingual static resource loading. It is an initial shell, not the finished visual experience. Streaming chat, conversation history, dynamic message localization, onboarding wizard, polished empty/error/loading states, and accessibility review remain required before calling the UX milestone complete.

- [ ] Decide the Windows distribution trust model. For GitHub-hosted setup downloads, acquire a legitimate Authenticode signing identity, keep signing material in protected CI secrets, sign the installer and app executables, verify signatures/timestamps in CI, and continue publishing SHA-256 checksums. Do not claim this guarantees immediate SmartScreen reputation; unsigned alpha builds remain clearly labelled until signing is actually configured.
- [~] Establish UI localization before adding more workspaces: English/Turkish resource files, persisted language preference, five-area navigation, and the primary static UI keys are implemented. Complete resource-key extraction for dynamic catalog, hardware, diagnostics, job, and update messages; add localized errors/progress/accessibility labels. Avoid putting translations or UI labels into engine/catalog metadata.
- [~] Define a single Activity model for download, verify, install, load, inference, stop and update jobs. `GET /v1/activity` and `activity-v1` currently provide a validated point-in-time view of download/update jobs and engine state; persistent event history, inference events, retry metadata, and consistent cancellation across all job types remain open.

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
- [ ] Replace the built-in metadata allowlist with a reviewed, versioned authoring manifest that generates API metadata and install allowlist from one source; arbitrary or remote executable manifests must remain prohibited.
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
