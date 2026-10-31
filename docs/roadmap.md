# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Current baseline and visible progress — 2026-10-31

**Published baseline:** `v0.1.26-alpha` (`a739155`). The user confirmed that its main and tagged publisher workflows both passed. That release includes the interim Stage 12.5 hardware-evidence and catalogue UI slice; the current guided-setup refactor is a new local change. Manual screen-reader review and real model/GPU acceptance remain open. The release contains the hardware-v1 contract, NVIDIA driver/compute-capability/VRAM evidence, plus the previously published five-area dashboard, local streaming chat, persistent Activity/jobs, live CPU/RAM/engine telemetry, EN/TR onboarding/accessibility, embedded component manifest v1, explicit Runtime Profiles, and persistent FIFO install queue with duplicate protection and safe restart recovery. Hosted CI uses fake engines and does not prove real GPU/model task compatibility.

**Current work:** Stage 12 hardware evidence is published. On the target RTX 3060 Laptop GPU, CUDA-backed Qwen3 4B inference has run with external NVIDIA process/utilization evidence and approximately 85–91 generated tokens/s in recent engine logs; the application still does not parse a per-layer CUDA offload count, and KV-cache restore/context-limit behavior remains open. Stage 12.5 has shipped the hardware evidence hierarchy and curated multilingual model shortlist; this working update simplifies the first-run path and makes the next action explicit. Its remaining gates are manual screen/accessibility review and recorded repeatable real-model CPU/GPU acceptance on the target computer. The catalogue still uses immutable single-file GGUF pins; multi-file multimodal, ONNX, SafeTensors and non-llama engine installers still require dedicated strategies. `/v1/catalog` remains backward compatible.

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
| 10 | Catalog v2, simplified Models UI and extensible multi-model pinned GGUF allowlist | [x] Published; main and tagged four-platform CI succeeded | `v0.1.21-alpha` |
| 11 | Shared Engine Adapter lifecycle; keep llama.cpp as first adapter | [x] Published; main and tagged four-platform CI passed | `v0.1.22-alpha` |
| 12 | Hardware Compatibility & Evidence; NVIDIA driver/VRAM probes and real RTX 3060 acceptance | [~] CUDA-backed inference observed on target GPU; app-side layer count and KV-cache/context acceptance remain open | `v0.1.23-alpha` |
| 12.5 | Product-readiness review: clearer UX, hardware evidence hierarchy, Chat onboarding and curated model catalogue | [~] Interim slice shipped; guided setup simplification implemented locally; manual UI/accessibility and real-model acceptance remain open | `v0.1.24-alpha` + current UX pass |
| 12.5.1 | Unified build-once release pipeline and one-screen CI report | [x] Build-once/publish-once flow shipped and confirmed in `v0.1.25-alpha` / `v0.1.26-alpha` | `v0.1.25-alpha` |
| 13 | Local MCP stdio MVP for the user's other project | [ ] Not started; gated on Stage 12.5 manual acceptance | `v0.1.27-alpha` (planned next) |
| 14 | Documents, embeddings and local RAG | [ ] Not started | Later alpha |
| 15 | Vision and Speech pilots | [ ] Not started | Later alpha |
| 16 | Image generation workspace and adapter | [ ] Not started | Later alpha |
| 17 | Video understanding (frames + audio + vision) | [ ] Not started | Later alpha |
| 18 | Music and advanced audio generation | [ ] Not started | Later alpha |
| 19 | Signing, provenance, cross-platform install/update/recovery parity and v1.0 acceptance | [ ] Not started | v1.0 gate |

### Stage 12.5 acceptance checklist — Product readiness before MCP

- [x] Replace the large always-open hardware dump with a plain-language device summary and an expandable technical evidence section.
- [x] Explain the distinction between OS adapter inventory, NVIDIA driver/VRAM evidence, live system-wide telemetry, configured backend and engine-log offload evidence.
- [x] Add a curated text-chat shortlist for the reported RTX 3060 Laptop GPU (6 GB VRAM) / 16 GB RAM profile: Qwen3 4B Q4_K_M as the balanced candidate, Gemma 4 E2B QAT Q4_0 as an alternative, and Qwen2.5 1.5B Q4_K_M as the compact CPU-first fallback.
- [x] Pin model files to immutable Hugging Face revisions with reviewed exact byte sizes and SHA-256 digests; keep all three models marked as not validated on the user's physical machine until real tasks are run.
- [x] Display a conservative starting context for each model; do not claim full GPU fit or multimodal app support from model-card capabilities alone.
- [x] Localize the curated model descriptions and evidence explanations in English and Turkish; add explicit Chat-page actions to choose a model or open Activity.
- [x] Reduce the required setup path to profile → ready engine → first message; keep device inspection optional and enable only the next incomplete action.
- [x] Make Chat show only the currently required next action; provide direct Models → Activity → Chat continuation actions.
- [ ] Manually review every screen at desktop and narrow widths, including keyboard navigation, focus order, screen-reader labels, and empty/error/loading states.
- [ ] Install each shortlist candidate on the target Windows device; record download verification, CPU baseline, CUDA start, actual layer offload, context, generation latency and peak VRAM.
- [ ] Only after the UI review and at least one complete local Chat path passes, proceed to Stage 13 MCP stdio.

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

The default product workflow remains task-first: users choose a task/workspace, the runtime helps select a compatible model and engine profile, Activity shows preparation/execution, and the Chat or task workspace only offers controls supported by the selected model's capabilities. The next feature milestone is MCP stdio, gated by Stage 12.5 manual acceptance and the live single-run release pipeline confirmation. No additional engine family is added until adapter, profile, queue, hardware-evidence and distribution gates are ready.

## Distribution status

- The `v0.1.2-alpha` milestone introduced the Windows per-user setup installer `local-ai-runtime-windows-x64-setup.exe`; the current published baseline is `v0.1.24-alpha`. The user has confirmed installation, launch and a Qwen2.5 starter-model chat on one Windows environment. This is not evidence for both Windows 10 and 11, real GPU inference, or every upgrade/uninstall path. Publisher signing remains outstanding. Always check the [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) for current assets.
- The Windows shell hosts the dashboard in WebView2, starts the C++ runtime without a console window, and uses a tray menu for reopen, update check and graceful exit. Clean Windows 10/11 manual acceptance remains a release gate.
- The managed catalogue contains llama.cpp CPU/CUDA packages and three pinned text-chat GGUF candidates (Qwen3 4B, Gemma 4 E2B and Qwen2.5 1.5B). Manifest v1 is embedded at build time. It is a curated shortlist, not a general catalogue of every model or C/C++ engine. Real model/CPU/GPU task evidence is still required per declared platform/profile.
- The dashboard has five destinations (Overview, Chat, Models, Activity, Settings), local EN/TR resources, first-run onboarding, accessibility basics, persistent jobs and resource telemetry. Manual screen-reader review remains open. See [product information architecture](product-information-architecture.md) and [UI/UX principles](ui-ux.md).
- Hosted CI uses fake engines and deliberately does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference requires separately recorded task validation.
- Windows installed-app updates have verification, backup, health-check, rollback and interruption recovery. Linux/macOS currently verify and stage packages but still require manual archive replacement. Windows Authenticode signing and release provenance remain outstanding.

## Historical foundation status

`v0.1.12-alpha` introduced the five-area UI and first `/v1/activity` snapshot. `v0.1.13-alpha` added SSE streaming and local conversation history. `v0.1.14-alpha` added durable component jobs and interrupted-state recovery. `v0.1.15-alpha` unified runtime operation jobs. `v0.1.16-alpha` added resource monitoring. `v0.1.17-alpha` expanded onboarding, localization and accessibility and included CI diagnostics/process cleanup. `v0.1.18-alpha` published the manifest-driven catalogue. The Runtime Profile milestone shipped in `v0.1.19-alpha`, the persistent install queue in `v0.1.20-alpha`, Catalog v2 in `v0.1.21-alpha`, Shared Engine Adapter in `v0.1.22-alpha`, NVIDIA driver evidence in `v0.1.23-alpha`, and the interim product-readiness UI/model shortlist in `v0.1.24-alpha`; real RTX 3060 inference/offload is still pending.

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
- [ ] Add engine families incrementally under the adapter contract: consider `stable-diffusion.cpp` for image generation, `whisper.cpp`/`sherpa-onnx` for speech, OpenCV/OCR and embeddings/reranking for knowledge workflows. Evaluate music/SFX and video runtimes separately. Each needs its own licence review, catalog metadata, process/lifecycle tests, platform builds, real task smoke test, and documented hardware requirements before being marked supported.
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
