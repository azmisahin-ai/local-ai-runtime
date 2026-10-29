# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Distribution status

- The earlier `v0.1.1-alpha` release contains **portable archives**. This source revision adds a Windows per-user setup installer; it is considered published only when a tagged CI run succeeds and the release includes `local-ai-runtime-windows-x64-setup.exe`. The [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) is authoritative for the current download assets.
- The new Windows shell hosts the existing dashboard in WebView2, starts the C++ runtime without a console window, and uses a tray menu for reopen, update check, and explicit graceful exit. The installer/payload must still pass tagged Windows CI and a clean Windows 10/11 manual acceptance run before the site calls it ready for general users.
- The managed catalogue currently starts with official `llama.cpp` CPU/CUDA assets and one pinned Qwen2.5 1.5B GGUF model. It is not yet a catalogue of every relevant C/C++ engine or model.
- Hardware detection and evidence-backed model recommendations are not implemented yet.
- Hosted CI uses a fake OpenAI-compatible engine and intentionally does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference therefore requires a separate maintainer validation run.
- Windows tagged portable packages support user-confirmed update apply/restart with checksum-verified backup and rollback. Linux/macOS currently verify and stage an update but require manual archive replacement.

A release is a user-ready desktop install only when its tagged CI includes the setup executable and has passed install/payload checks. Until then, the website must continue to offer the portable fallback and must not describe the WinForms shell as publicly available. Clean Windows 10/11 user-session acceptance is still required before calling the desktop experience mature.

## Priority 0 — keep the release pipeline green

- [x] Fix the Windows PowerShell 5.1 publisher smoke-test regression and confirm the canonical four-target pipeline passes. `d97b8ee` is the last confirmed green publisher/roadmap baseline.
- [x] Keep one maintainer command and one stable bundle filename (`local-ai-runtime-update.bundle`). A push uses a normal fast-forward, never force-pushes, and stops at a failed CI gate.
- [ ] Validate the new desktop shell and per-user setup installer on Windows CI before creating its next alpha tag. A failed main build must never create a release tag.

## Priority 1 — real Windows desktop installation (first product milestone)

- [ ] Validate the new self-contained WebView2/WinForms desktop host and Inno Setup per-user installer through Windows CI; include Start menu entry and optional desktop shortcut. The workflow is prepared to package, install, verify, and uninstall it, but this checkbox stays open until the new workflow run succeeds.
- [ ] Validate console-free launcher invocation, single-instance activation, tray reopen/update actions, close-to-tray behavior, explicit graceful exit, and isolated user-data storage in Windows CI/manual acceptance.
- [ ] Finish a dedicated Windows desktop launch smoke test and manual clean-machine acceptance on Windows 10/11. Confirm tray lifetime, upgrade, rollback, and uninstall behavior on actual user sessions.
- [ ] Add a clear first-run wizard, progress/error states, repair/retry actions, and a user-readable diagnostics export. Do not ask users to run developer tests.
- [ ] Update the entire installed desktop application (shell + runtime) atomically, with rollback and interruption recovery, without replacing settings, engine binaries, or model weights. Until that exists, installed builds must use the full setup installer; the portable ZIP apply action is disabled for desktop-shell installs.
- [x] Make the installer/uninstaller keep mutable user data outside the application directory and preserve it by default.
- [ ] Add a separate explicit option to remove user data during uninstall and verify it safely.
- [ ] Validate install, launch, close/reopen, upgrade, forced interruption recovery, rollback, and uninstall on clean Windows 10/11 x64 virtual machines. Verify that user data survives an application update.

**Windows milestone acceptance:** a new user downloads one installer from the project site, installs it, launches from Start, sees a desktop window without a console, installs a suitable CPU engine/model through the UI, produces a real chat response, and updates from the UI without manually moving ZIP files or editing JSON.

## Priority 2 — hardware-aware first run and compatibility reporting

- [ ] Detect OS/architecture, CPU features, system RAM, available disk, GPU vendor/device, driver version, and VRAM where available. Keep detection local; do not upload machine data.
- [ ] Expose evidence states as `detected`, `available`, `configured`, and `tested`.
- [ ] Recommend an engine/backend/model based on explicit memory and runtime constraints. Offer a CPU fallback when acceleration is unavailable or not verified; never infer backend support from GPU presence alone.
- [ ] Explain estimated download size, model memory needs, context-size tradeoffs, license, and the reason a recommendation is or is not suitable.
- [ ] Record repeatable baseline profiles, including the project's low-hardware target and the referenced RTX 3060 6 GB / 16 GB RAM profile. A profile is a test record, not a universal guarantee.

## Priority 3 — extensible engine and model catalogue

The metadata contract and distribution boundaries live in [engine distribution strategy](engine-distribution.md).

- [x] Separate runtime release, engine packages, and model weights; keep large models out of the runtime installer.
- [x] Starter catalogue: official `llama.cpp` CPU/CUDA assets and a pinned Qwen2.5 1.5B Q4_K_M GGUF model, with license acknowledgement, size limits, HTTPS, SHA-256, safe extraction, progress, cancellation, and user-data installation paths.
- [ ] Formalize a versioned engine adapter/catalogue schema with OS, architecture, backend, source/version, licence, hash/signature, dependencies, required RAM/VRAM, capabilities, known limits, and test evidence.
- [ ] Run a real `llama-server` + GGUF CPU inference smoke test on Windows and one additional CPU environment; record the exact package/model and local report.
- [ ] Run CUDA validation with GPU, driver, VRAM, model quantization, context and backend evidence. `--n-gpu-layers auto` alone is not proof of GPU inference.
- [ ] Expand model choices with size/quantization and hardware guidance; recommend only combinations with evidence and keep “unverified” combinations clearly labelled.
- [ ] Add engine families incrementally under the adapter contract: consider `stable-diffusion.cpp`, `audio.cpp`, ONNX Runtime, OpenCV/OCR and embeddings/vector search. Each needs its own licence review, catalogue entry, process/lifecycle tests, platform builds, real task smoke test, and documented hardware requirements before being marked supported.
- [ ] Resume interrupted downloads and probe a newly installed engine/model before marking the combination tested.

## Priority 4 — safe, understandable updates

- [x] A successful `main` push builds/tests all four targets and updates the project website after package gates pass.
- [x] The maintainer publishing flow waits for green `main` CI before creating an alpha tag; the tag workflow then gates GitHub Release publication on the same platform jobs.
- [x] The dashboard checks release metadata at startup, offers a manual check, and verifies matching official release archives by size and SHA-256 before staging.
- [x] Tagged portable Windows releases require explicit user confirmation and have backup, health-check, rollback, and interrupted-update recovery.
- [ ] Move the Windows update flow to the installed-app location and provide a clear “Download update / Restart to update” experience with visible progress and a recovery message.
- [ ] Add verified apply/recovery for Linux and macOS before showing one-click installation controls there.
- [ ] Plan signed installers and release provenance/attestation. SHA-256 is an integrity check, not publisher authentication. Do not silently replace executables until trust, recovery, and update threat models are independently tested.

## Priority 5 — cross-platform desktop parity

- [ ] After the Windows installed application and update lifecycle pass clean-machine tests, provide native installation and app lifecycle for Linux and macOS.
- [ ] Keep the same first-run, hardware guidance, engine/model catalogue, update status, and user-data preservation semantics across platforms.
- [ ] Add audio, vision, embeddings, streaming chat, and optional MCP only after the desktop install, engine lifecycle, compatibility evidence and security boundaries remain stable.

## Definition of done for user-facing claims

- “Installer” means a tested installer on a clean supported OS, not just a ZIP archive.
- “Desktop app” means a window users can open/close from the OS, with predictable runtime/process behavior and no required terminal.
- “Supported engine/model/backend” means a repeatable real task test exists for the declared platform/hardware profile.
- “Automatic update” means verified download, explicit or policy-defined user consent, safe apply/restart, rollback, and preserved user data; a release notification alone is not an updater.
