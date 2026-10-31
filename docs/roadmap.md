# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Current baseline and visible progress — 2026-10-31

**Published baseline:** `v0.1.31-alpha` (`e18a084`). The user supplied a PowerShell publisher transcript confirming that the single unified main pipeline passed and published the exact tested artifact set; workflow: <https://github.com/azmisahin-ai/local-ai-runtime/actions/runs/41234340882>. The imported `v0.1.31-alpha` release is the source baseline for this Stage 13A work. The preceding `v0.1.30-alpha` handoff remains historical, not the latest release state.

**Hardware baseline:** On the user's Windows x64 device (RTX 3060 Laptop GPU 6 GB, 16 GB RAM, driver 616.92, compute capability 8.6), Qwen3 4B with llama.cpp CUDA 13 has generated real answers; recent engine timing records are around 85–91 generated tokens/s. Previous NVIDIA process/utilization observations support that CUDA work occurred, but Local AI Runtime still lacks verified per-layer offload evidence in its current-session status, and KV-cache/context restore remains an open follow-up. Do not equate system-wide GPU telemetry with process-specific offload proof.

**Priority change:** The user explicitly asked to stop spending disproportionate time on small Chat/Markdown polish and to prioritize integrating other AI engine families. Freeze non-critical Chat/Markdown/language polish. Fix only security, data-loss, engine lifecycle, or basic inference correctness issues. Residual Stage 12/12.5 checks remain tracked but must not block Stage 13A audio work.

**Next engineering focus:** Stage 13A is an `audio.cpp` pilot. A typed task adapter, normalized ASR/TTS routes, a CPU lifecycle contract test, and the `audio-cpp-release-set` installer strategy are now implemented and covered by hosted tests. No real audio.cpp archive has yet been installed on Windows; no CUDA task or Qwen3-ASR Turkish transcription has been validated. The runtime catalog still has only `llama.cpp` active entries until exact byte-size pins and the Windows package smoke are available. Research and hardware decision records remain in [Engine Integration Research](engine-integration-research.md).

### Product delivery checklist

| Stage | Scope | State | Target |
|---|---|---|---|
| 1–2 | Five-area UI, local streaming Chat and conversation history | [x] Published | `v0.1.13-alpha` |
| 3–4 | Persistent Job Manager and unified runtime Activity | [x] Published | `v0.1.15-alpha` |
| 5 | Runtime Resource Manager | [x] Published | `v0.1.16-alpha` |
| 6 | EN/TR, accessibility and first-run guide | [x] Published | `v0.1.17-alpha` |
| 6.1 | CI failure diagnostics and Windows process cleanup | [x] Published | `v0.1.17-alpha` |
| 7 | Embedded Component Catalog Manifest v1 | [x] Published | `v0.1.18-alpha` |
| 8 | Runtime Profile, explicit model/backend selection and Chat readiness | [x] Published; main and tagged workflow succeeded after Release-job rerun | `v0.1.19-alpha` |
| 9 | Persistent install queue, duplicate/dependency handling and reliable retries | [x] Published; main and tagged four-platform CI passed | `v0.1.20-alpha` |
| 10 | Catalog v2, simplified Models UI and extensible multi-model pinned GGUF allowlist | [x] Published | `v0.1.21-alpha` |
| 11 | Shared Engine Adapter lifecycle; keep llama.cpp as first adapter | [x] Published | `v0.1.22-alpha` |
| 12 | Hardware Compatibility & Evidence; NVIDIA driver/VRAM probes and real RTX 3060 acceptance | [~] CUDA-backed Qwen3 inference observed; app-side layer count and KV-cache/context acceptance remain open | `v0.1.23-alpha` onward |
| 12.5 | Product readiness: first-run flow, evidence hierarchy, Chat correctness and safe assistant Markdown | [~] Core onboarding/Chat fixes shipped through `v0.1.30-alpha`; residual responsive/accessibility review and latest-release manual spot-check remain non-blocking for engine work | `v0.1.24-alpha` → `v0.1.30-alpha` |
| 12.5.1 | Unified build-once release pipeline and one-screen CI report | [x] Build-once/publish-once flow shipped and repeatedly confirmed | `v0.1.25-alpha` onward |
| 13A | Audio task engine, typed task APIs and safe Windows release-set installer | [~] Typed adapter/routes, exact asset allowlist, paired archive installer and CPU lifecycle/task fake-engine tests pass; real Windows archive install and native CPU package lifecycle remain pending | Current engineering milestone |
| 13B | Turkish ASR and TTS with audio.cpp | [ ] Not started; Qwen3-ASR-0.6B first, explicit `tr`, then CUDA; Chatterbox TTS second | After native CPU/CUDA 13A gates |
| 13C | ASR alternatives comparison | [ ] Not started; whisper.cpp first alternative, sherpa-onnx only if justified | After 13B |
| 14 | Image generation | [ ] Not started; stable-diffusion.cpp, SD 1.5 / 512x512 pilot | Later alpha |
| 15 | Vision and OCR | [ ] Not started; ONNX Runtime + OpenCV and one pinned model | Later alpha |
| 16 | Media and video processing | [ ] Not started; FFmpeg as audited media infrastructure, not AI model | Later alpha |
| 17 | Documents, embeddings and local RAG | [ ] Not started | Later alpha |
| 18 | 3D reconstruction | [ ] Not started; COLMAP + Open3D | Later alpha |
| 19 | Music and advanced audio generation | [ ] Not started; audio.cpp ACE-Step only after speech tasks pass and VRAM is measured | Later alpha |
| 20 | MCP stdio, Agent / Planning and orchestration of tested local tasks | [ ] Not started; do not expose untested engines as tools | Later alpha |
| 21 | Signing, provenance, cross-platform install/update/recovery parity and v1.0 acceptance | [ ] Not started | v1.0 gate |

### Stage 13A acceptance checklist — audio.cpp task runtime foundation

- [x] Keep `EngineAdapter` v1 and llama.cpp Chat behavior unchanged; register audio.cpp with a separate lifecycle adapter and no Chat completions path.
- [x] Add typed `asr.transcribe` and `tts.synthesize` request/result contracts with fixed routes, explicit capability declarations, safe model/language tokens, bounded request sizes and in-memory WAV handling.
- [x] Add normalized task routes `POST /v1/tasks/asr/transcribe` and `POST /v1/tasks/tts/synthesize`; keep audio bytes/text out of Activity diagnostics.
- [x] Extend Activity job kinds to persist audio-task operations.
- [x] Add an allowlisted `audio-cpp-release-set` manifest strategy: CPU server archive or CUDA server+CUDART pair, exact upstream filenames/URLs/SHA-256, Windows x64 only, bounded exact byte sizes.
- [x] Implement HTTPS download, exact-size/SHA-256 verification, safe archive extraction, safe merge of package pairs, per-user staging, activation rollback and non-Chat engine registration.
- [x] Add a managed fake-engine CPU lifecycle test covering `/health`, `/v1/models`, stop and restart.
- [x] Add end-to-end typed task HTTP smoke coverage for ASR multipart + `tr`, malformed model ID rejection, TTS WAV artifact, Activity persistence and Chat isolation.
- [ ] Obtain exact byte sizes for the v0.9.3 release assets from authoritative release metadata and add catalog records only after those values are independently confirmed; do not use rounded release-page MB labels.
- [ ] On Windows x64, download and install the exact official CPU archive, verify executable, start/health/models/stop/restart and record process/runtime evidence.
- [ ] On the same Windows target, install the CUDA 13.3 server+CUDART pair, validate CPU fallback on the CUDA-enabled binary, then CUDA process readiness. Record driver/backend evidence separately.
- [ ] Only after native CPU and CUDA lifecycle gates, pin Qwen3-ASR model revision/files/license/hash; run the same licensed WAV with reference transcript on CPU, then CUDA.

### Stage 12.5 acceptance checklist — Product readiness (non-blocking for Stage 13A)

- [x] Replace the large always-open hardware dump with a plain-language device summary and an expandable technical evidence section.
- [x] Explain the distinction between OS adapter inventory, NVIDIA driver/VRAM evidence, live system-wide telemetry, configured backend and engine-log offload evidence.
- [x] Add a curated text-chat shortlist for the reported RTX 3060 Laptop GPU (6 GB VRAM) / 16 GB RAM profile.
- [x] Pin existing text-chat model files to immutable revisions with reviewed sizes and SHA-256 digests.
- [x] Reduce the required setup path to profile → ready engine → first message; keep device inspection optional.
- [x] Validate Qwen3 normal Chat returns a visible final answer on `v0.1.28-alpha`.
- [x] Manually validate core Markdown presentation (headings, emphasis, lists and fenced C++ code) on `v0.1.29-alpha`; raw HTML remains inert by the renderer contract.
- [~] `v0.1.30-alpha` entity normalization and locale-instruction follow-up is published; post-release manual spot-check still open. Do not spend another release solely on cosmetic language/inline-formatting differences.
- [ ] Manually review desktop/narrow UI, keyboard focus, screen-reader labels and empty/error/loading states when time permits; this does not block Stage 13A.
- [ ] Keep Stage 12 app-side GPU layer-offload evidence and KV-cache/context acceptance as a hardware follow-up; do not block audio CPU/CUDA package work.

### Stage 12.5.1 acceptance checklist — single-run release pipeline

- [x] Plan the alpha version before compilation and reuse an existing tag at the same commit when a workflow is rerun.
- [x] Build, test and package Windows, Linux, macOS x64 and macOS arm64 once per main push.
- [x] Upload uniquely named immutable platform candidate artifacts and publish those exact files after all platform gates pass.
- [x] Create/verify the annotated tag against the tested commit, generate SHA-256 checksums and publish the Release without a tag-triggered second build.
- [x] Deploy GitHub Pages after the same-run Release gate and write a final workflow summary for every stage.
- [x] Reduce the local PowerShell publisher to fast-forward push, one-run status polling and release verification.
- [x] Add version-planner and unified-workflow regression tests to CTest.
- [x] Verify the unified workflow on GitHub: one run, all platform targets green, tag on the tested SHA, and the same tested artifacts published.


### Stage 9 acceptance checklist

- [x] Replace per-install worker threads with one bounded FIFO install worker shared by catalogue components and runtime-update packages.
- [x] Make duplicate start requests for the same queued/active component idempotent and return the existing job ID.
- [x] Persist queued jobs, validate embedded manifest identity, target support and recorded license acceptance at startup, and resume only jobs that had not started.
- [x] Mark an active transfer as canceled/interrupted on shutdown and do not replay partially completed work automatically.
- [x] Add queue position and reviewed dependency plan to `jobs-v1`; display queue state and dependency metadata in Activity and Models.
- [x] Declare CUDA 12/13 runtime archives in manifest metadata and resolve/verify/merge them within their parent llama.cpp install job.
- [x] Add FIFO, one-worker, duplicate, restart recovery and active-job non-replay core tests.
- [x] Pass Windows x64, Linux x64, macOS x64 and macOS arm64 main/tag CI; publish `v0.1.20-alpha` (the release follows a four-platform green main and tagged workflow).
- [ ] Validate the queue manually on Windows by adding CPU, CUDA 12 and CUDA 13 packages in quick succession; confirm they run sequentially and only the selected Runtime Profile becomes active.

### Stage 11 acceptance checklist

- [x] Add an explicit reviewed `adapter_id` to engine configuration and migrate older configs deterministically.
- [x] Introduce the shared adapter contract for config validation, start/stop/status, readiness endpoints and upstream chat-completion routing.
- [x] Implement `llama.cpp` as the first built-in adapter without changing its launch arguments, GGUF path or existing OpenAI-compatible chat behavior.
- [x] Keep a separate OpenAI-compatible local-process base adapter for generic fake/test configurations; do not present it as an additional catalog engine product.
- [x] Reject unknown adapter IDs before launch or config replacement.
- [x] Expose the resolved adapter ID in `/v1/engines` and test legacy config migration, adapter routing and process lifecycle delegation.
- [x] Document the adapter extension contract and explicitly defer Ollama, ONNX Runtime and media adapters until their protocol, installer and real-task evidence are ready.
- [x] Pass Windows x64, Linux x64, macOS x64 and macOS arm64 main/tag CI; publish `v0.1.22-alpha`.

### Stage 8 acceptance checklist

- [x] Persist active engine component, model component and context size in `engines.json`.
- [x] Install engine packages side-by-side without silently switching the active backend.
- [x] Install or re-verify model files without silently changing the active Chat model.
- [x] Select an installed engine package and installed model explicitly in Models.
- [x] Apply the selected profile only when engines are stopped; verify model size, SHA-256 and GGUF header before activation.
- [x] Block engine start when no Chat model is selected.
- [x] Disable Chat Send until the selected engine reports ready; guard streaming and non-streaming APIs as well.
- [x] Record attempted inference when the engine/model is not ready; preserve the typed message in the UI while send is disabled.
- [x] Keep manual llama.cpp setup as an Advanced/custom path, separate from managed catalog profiles.
- [x] Add a versioned runtime-profile API schema and regression tests for persisted component IDs (including dots in IDs).
- [ ] Pass Windows x64, Linux x64, macOS x64 and macOS arm64 main/tag CI; then publish the candidate.

The Stage 8 Runtime Profile milestone is published in `v0.1.19-alpha`. Stage 9 is published in `v0.1.20-alpha` after the four-platform publisher workflow passed. Its Windows acceptance check should still confirm rapid successive CPU/CUDA package requests on a real user machine. The queue core test must remain platform-neutral: CUDA availability is asserted in manifest contract tests, while sequential queue/cancel/re-queue behavior uses components supported on every CI host.

The default product workflow remains task-first: users choose a task/workspace, the runtime helps select a compatible model and engine profile, Activity shows preparation/execution, and each task workspace only offers controls supported by the selected model's capabilities. Stage 13A audio.cpp foundations are implemented in code and contract-tested; native Windows package lifecycle, exact release asset byte sizes and a real Turkish model task remain open. No audio.cpp component is enabled in the shipped catalog until these target and provenance gates pass. See [engine integration research](engine-integration-research.md) and [next-session handoff](next-session-handoff.md).

## Distribution status

- The `v0.1.2-alpha` milestone introduced the Windows per-user setup installer `local-ai-runtime-windows-x64-setup.exe`; the current published baseline is `v0.1.31-alpha` (`e18a084`; unified workflow 41234340882). The user has confirmed installation, launch and local Qwen/Qwen3 chat on one Windows environment. This is not evidence for both Windows 10 and 11, real GPU inference, or every upgrade/uninstall path. Publisher signing remains outstanding. Always check the [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) for current assets.
- The Windows shell hosts the dashboard in WebView2, starts the C++ runtime without a console window, and uses a tray menu for reopen, update check and graceful exit. Clean Windows 10/11 manual acceptance remains a release gate.
- The managed catalogue contains llama.cpp CPU/CUDA packages and three pinned text-chat GGUF candidates (Qwen3 4B, Gemma 4 E2B and Qwen2.5 1.5B). Manifest v1 is embedded at build time. It is a curated shortlist, not a general catalogue of every model or C/C++ engine. Real model/CPU/GPU task evidence is still required per declared platform/profile.
- The dashboard has five destinations (Overview, Chat, Models, Activity, Settings), local EN/TR resources, first-run onboarding, accessibility basics, persistent jobs and resource telemetry. Manual screen-reader review remains open. See [product information architecture](product-information-architecture.md) and [UI/UX principles](ui-ux.md).
- Hosted CI uses fake engines and deliberately does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference requires separately recorded task validation.
- Windows installed-app updates have verification, backup, health-check, rollback and interruption recovery. Linux/macOS currently verify and stage packages but still require manual archive replacement. Windows Authenticode signing and release provenance remain outstanding.

## Historical foundation status

`v0.1.12-alpha` introduced the five-area UI and first `/v1/activity` snapshot. `v0.1.13-alpha` added SSE streaming and local conversation history. `v0.1.14-alpha` added durable component jobs and interrupted-state recovery. `v0.1.15-alpha` unified runtime operation jobs. `v0.1.16-alpha` added resource monitoring. `v0.1.17-alpha` expanded onboarding, localization and accessibility and included CI diagnostics/process cleanup. `v0.1.18-alpha` published the manifest-driven catalogue. The Runtime Profile milestone shipped in `v0.1.19-alpha`, the persistent install queue in `v0.1.20-alpha`, Catalog v2 in `v0.1.21-alpha`, Shared Engine Adapter in `v0.1.22-alpha`, NVIDIA driver evidence in `v0.1.23-alpha`, and the product-readiness UI/model shortlist in `v0.1.24-alpha`; Qwen3 chat/Markdown follow-ups continued through `v0.1.30-alpha`; the subsequent `v0.1.31-alpha` release recorded the Stage 13A roadmap/handoff update. CUDA-backed Qwen3 inference has been observed on the RTX 3060, while app-side layer-offload evidence and repeatable device-profile validation remain open.

- [x] Fix the Windows PowerShell 5.1 publisher smoke-test regression and confirm the canonical four-target pipeline passes. `f841c80` was the confirmed green publisher/installer baseline when this roadmap update began (`v0.1.2-alpha`).
- [x] Keep one repository-local maintainer command. It pushes only normal fast-forwards, never force-pushes, and stops at a failed CI gate. Git bundles are optional one-time source-transfer artifacts, not part of routine publishing.
- [x] Validate the desktop shell and per-user setup installer on Windows CI, including silent install, payload verification and uninstall. `v0.1.2-alpha` is published from the first green installer pipeline.
- [x] Keep publisher/installer tests green on every main push. A failed platform build must never create a release tag.
- [~] Replace the former main-build + tag-build sequence with a single build-once pipeline: version is planned before compilation; platform jobs upload immutable candidate artifacts; the release job tags the tested commit and publishes those exact files; Pages deploys after release publication; one final summary reports every stage. The repo-local publisher now only pushes a fast-forward commit and verifies the one run. Local contract/unit tests pass; live GitHub CI verification of this pipeline revision remains pending.

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
- [~] Add first NVIDIA driver/compute-capability/VRAM evidence through a separate local `nvidia-smi` probe; DXGI/DRM adapter inventory remains a separate source. CPU-feature and CUDA-runtime compatibility probes, broader AMD/Intel coverage, and native macOS GPU enumeration remain open.
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
- [x] Replace the built-in metadata allowlist with the reviewed, build-time embedded `catalog/manifest-v1.json` source; `v0.1.18-alpha` drives API metadata, model pins, engine strategy dispatch and platform install targets from it. Arbitrary or remote executable manifests remain prohibited.
- [ ] Run a real `llama-server` + GGUF CPU inference smoke test on Windows and one additional CPU environment; record the exact package/model and local report.
- [x] Implement exact official CUDA server + companion runtime asset selection, SHA-256 verification, safe extraction/merge, aggregate progress, conservative free-space checks, diagnostic asset hashes, and regression coverage for Windows/Linux x64 asset naming. The real GPU task test remains a separate gate.
- [ ] Run CUDA validation with a real NVIDIA GPU, driver, VRAM, model quantization, context and backend evidence. `--n-gpu-layers auto` alone is not proof of GPU inference.
- [ ] Expand model choices with size/quantization and hardware guidance; recommend only combinations with evidence and keep “unverified” combinations clearly labelled.
- [ ] Add engine families incrementally under the task-adapter and fail-closed installer design. The research decision is `audio.cpp` first (ASR/TTS), then `stable-diffusion.cpp`, ONNX Runtime + OpenCV, FFmpeg media infrastructure, COLMAP/Open3D, advanced audio/music, and MCP/Agent orchestration. See [engine integration research](engine-integration-research.md). Each needs separate engine/model license review, exact pinned assets and hashes, task/lifecycle tests, platform builds, and real task evidence before being marked supported.
- [ ] Resume interrupted downloads and probe a newly installed engine/model before marking the combination tested.

## Priority 4 — safe, understandable updates

- [~] A successful `main` push builds/tests/packages all four targets once, creates the alpha tag only after the platform gates pass, publishes those exact artifacts and then updates the project website in the same workflow. The live GitHub run must confirm this implementation before marking it fully accepted.
- [x] The release job verifies that an existing tag belongs to the tested commit, writes SHA-256 checksums, and reuses the same-run artifacts instead of starting a tag-triggered second build.
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
