# Roadmap

This roadmap distinguishes code that exists from combinations that have been tested on real hardware. A green CI build is necessary, but it does not prove a real model, GPU backend, or installer works on a user's computer.

## Product promise

Local AI Runtime should feel like an installed desktop application, not a developer script. A user downloads it from the project website, installs it once, opens it from the Start menu or desktop, receives a hardware-informed engine/model recommendation, chats locally, and updates from within the app. Users must not need Git, PowerShell, a terminal, JSON editing, or maintainer smoke-test scripts.

The runtime, engines, and models remain separate versioned components. The catalogue must be honest about licenses, supported hardware, memory requirements, and evidence. “Detected” does not mean “supported”; “starts” does not mean “inference tested.”

## Current reality — `v0.1.0-alpha`

- The published packages are **portable archives**, not conventional OS installers.
- The dashboard is served locally and opens in the default browser. Windows launch currently uses a `.cmd`/PowerShell launcher; a native desktop window and fully managed application lifecycle are not implemented yet.
- The managed catalogue currently starts with official `llama.cpp` CPU/CUDA assets and one pinned Qwen2.5 1.5B GGUF model. It is not yet a catalogue of every relevant C/C++ engine or model.
- Hardware detection and evidence-backed model recommendations are not implemented yet.
- Hosted CI uses a fake OpenAI-compatible engine and intentionally does not download the real engine or the ~1.12 GB starter model. Real CPU/GPU inference therefore requires a separate maintainer validation run.
- Windows tagged portable packages support user-confirmed update apply/restart with checksum-verified backup and rollback. Linux/macOS currently verify and stage an update but require manual archive replacement.

The alpha is useful for previewing the runtime, but it is not yet the promised one-time-installed desktop experience. Do not describe console/script launchers as the finished UI.

## Priority 0 — restore and keep the release pipeline green

- [ ] Fix the Windows PowerShell 5.1 publisher smoke-test regression: the test intentionally runs a fake native command with a non-zero exit code, but must clear that expected status after all assertions pass so GitHub Actions does not mark a successful test as failed.
- [ ] Confirm the whole four-target `main` pipeline passes: Windows x64, Linux x64, macOS Intel x64, and macOS Apple Silicon arm64. Pages must deploy only after all package jobs pass.
- [ ] Keep one maintainer command and one stable bundle filename (`local-ai-runtime-update.bundle`). A push must use a normal fast-forward, never force-push, and stop on any failed CI gate.
- [ ] Only after `main` CI passes, create the next alpha tag and let the tag pipeline publish the four packages and `SHA256SUMS.txt`. A failed main build must never create a release tag.

## Priority 1 — real Windows desktop installation (first product milestone)

- [ ] Produce a per-user Windows installer (`.exe`) from the successful CI package. Default install must not require administrator elevation; include Start menu entry and optional desktop shortcut.
- [ ] Provide a dedicated desktop window using the existing HTML dashboard in an embedded WebView2 shell. Normal use must not expose CMD/PowerShell windows or require the user to keep a terminal open.
- [ ] Own the runtime and engine process lifecycle: single application instance; reopen focuses the existing window; the app can be closed/reopened safely; provide clear **Keep running** and **Exit and stop local engines** behavior (for example via a system-tray menu) so inference never stops unexpectedly or runs invisibly without user choice.
- [ ] Add a clear first-run wizard, progress/error states, repair/retry actions, and a user-readable diagnostics export. Do not ask users to run developer tests.
- [ ] Update and rollback the installed application atomically without replacing settings, engine binaries, or model weights. Protect against interruption and provide a safe recovery path.
- [ ] Uninstall application binaries and shortcuts cleanly; preserve settings, downloaded engines, and models by default, with a separate explicit option to remove user data.
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
