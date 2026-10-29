# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Distribution status

- The `v0.1.2-alpha` milestone introduced the Windows per-user setup installer `local-ai-runtime-windows-x64-setup.exe`; tagged CI passed the install/payload/uninstall checks. A clean Windows 10/11 user-session acceptance test, publisher signing, and installed-app update flow remain outstanding. Always check the [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) for the current assets.
- The new Windows shell hosts the existing dashboard in WebView2, starts the C++ runtime without a console window, and uses a tray menu for reopen, update check, and explicit graceful exit. The installer/payload passed tagged Windows CI; the site may offer it as an alpha, but it must not be described as a mature general-availability product until clean Windows 10/11 manual acceptance is complete.
- The managed catalogue currently starts with official `llama.cpp` CPU/CUDA packages and one pinned Qwen2.5 1.5B GGUF model. The catalog pairs the CUDA server archive with its official companion runtime/DLL archive, verifies both, and records their hashes; physical GPU inference remains unverified until a real hardware run. It is not yet a catalogue of every relevant C/C++ engine or model.
- The dashboard now distinguishes workspaces (tasks) from the component catalog (engines, models and dependencies). See [product information architecture](product-information-architecture.md) for the planned domains and metadata contract.
- Local hardware inventory and first-pass heuristic recommendations are implemented. Evidence-backed per-device live inference measurements and validated compatibility profiles remain in progress.
- Hosted CI uses a fake OpenAI-compatible engine and intentionally does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference therefore requires a separate maintainer validation run.
- Windows tagged portable packages support user-confirmed update apply/restart with checksum-verified backup and rollback. Linux/macOS currently verify and stage an update but require manual archive replacement.

The website can offer the Windows setup executable as an alpha because its tagged CI passes install/payload/uninstall checks; the portable ZIP remains a fallback. Clean Windows 10/11 user-session acceptance is still required before calling the desktop experience mature.

## Priority 0 — keep the release pipeline green

- [x] Fix the Windows PowerShell 5.1 publisher smoke-test regression and confirm the canonical four-target pipeline passes. `f841c80` was the confirmed green publisher/installer baseline when this roadmap update began (`v0.1.2-alpha`).
- [x] Keep one maintainer command and one stable bundle filename (`local-ai-runtime-update.bundle`). A push uses a normal fast-forward, never force-pushes, and stops at a failed CI gate.
- [x] Validate the desktop shell and per-user setup installer on Windows CI, including silent install, payload verification and uninstall. `v0.1.2-alpha` is published from the first green installer pipeline.
- [ ] Keep publisher/installer tests green on every main push. A failed main build must never create a release tag.

## Priority 1 — real Windows desktop installation (first product milestone)

- [x] Build the self-contained WebView2/WinForms desktop host and Inno Setup per-user installer; Windows CI packages, installs, verifies and uninstalls it, including the Start menu entry and optional desktop shortcut.
- [ ] Complete clean Windows 10/11 manual acceptance for first-run, WebView2 availability, user-session lifecycle, upgrade, rollback and uninstall before calling the installed app mature.
- [ ] Validate console-free launcher invocation, single-instance activation, tray reopen/update actions, close-to-tray behavior, explicit graceful exit, and isolated user-data storage in Windows CI/manual acceptance.
- [ ] Add a clear first-run wizard, progress/error states, repair/retry actions, and a user-readable diagnostics export. Do not ask users to run developer tests.
- [ ] Update the entire installed desktop application (shell + runtime) atomically, with rollback and interruption recovery, without replacing settings, engine binaries, or model weights. Until that exists, installed builds must use the full setup installer; the portable ZIP apply action is disabled for desktop-shell installs.
- [x] Make the installer/uninstaller keep mutable user data outside the application directory and preserve it by default.
- [ ] Add a separate explicit option to remove user data during uninstall and verify it safely.
- [ ] Validate install, launch, close/reopen, upgrade, forced interruption recovery, rollback, and uninstall on clean Windows 10/11 x64 virtual machines. Verify that user data survives an application update.

**Windows milestone acceptance:** a new user downloads one installer from the project site, installs it, launches from Start, sees a desktop window without a console, installs a suitable CPU engine/model through the UI, produces a real chat response, and updates from the UI without manually moving ZIP files or editing JSON.

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
- [ ] Move the Windows update flow to the installed-app location and provide a clear “Download update / Restart to update” experience with visible progress and a recovery message.
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
