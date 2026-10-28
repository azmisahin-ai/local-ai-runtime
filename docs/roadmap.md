# Roadmap

The roadmap distinguishes implemented code from platform/model combinations that have actually been tested. A build passing on GitHub Actions does not prove that CUDA, a particular GPU, or a particular model works.

## 0.1-alpha — first product foundation

- [x] C++20/CMake project and loopback HTTP API
- [x] JSON engine manifest and process start/stop/logging
- [x] Non-streaming chat proxy and `/v1/models` forwarding
- [x] Same-origin browser dashboard
- [x] Windows/Linux build helper and fake-engine core/HTTP tests
- [x] Fix WinSock 1/2 header ordering for MSVC
- [x] `/version` endpoint and stable-release update notification in dashboard
- [x] Windows/MSVC and Ubuntu/GCC CI on pull requests and non-main branch pushes; main pushes use the canonical build/test/package workflow to avoid duplicate compiles; Pages deploys only when site files change; separate weekly/manual experimental macOS compatibility workflow
- [x] Project Pages site and feature manifest
- [x] Tag-based Windows/Linux release workflow with SHA-256 manifest
- [x] Confirm hosted Windows/MSVC CI on the public repository (green at commit `1f020c4`)
- [x] Confirmed macOS CI passed once at commit `1f020c4`; current macOS checks are scheduled/manual and do not block each push
- [ ] Publish the first prerelease tag after the user-facing llama.cpp setup is ready; current workflow dry-run/package rehearsal is green
- [ ] Real `llama-server` + GGUF smoke test on Windows and one CPU-only environment; CI currently uses a fake engine and intentionally does not fetch external components
- [x] Dashboard setup form for an existing local `llama-server` and GGUF file; persists the engine configuration without hand-editing JSON
- [x] Runtime readiness state from the local `/health` endpoint (fallback to `/v1/models`) and chat gating in the dashboard
- [ ] Real CUDA smoke test with GPU model, VRAM, driver, model quantization and backend recorded
- [x] Startup readiness probe separated from process-running state (runtime health signal; real model loading still needs a real-engine smoke test)
- [x] Add extracted Windows/Linux archive checks for version, dashboard/launcher files, and a live `/health` response in the release workflow
- [x] Auto-build Windows/Linux preview artifacts on each `main` push; public releases remain tag-gated
- [x] Surface preview builds from the website while no public release exists

## 0.2-alpha — product reliability

- [ ] Bounded request concurrency, graceful HTTP shutdown and cancellation-safe child cleanup
- [ ] Windows Job Object process cleanup and graceful stop policy
- [x] Basic engine readiness probe and dashboard chat gating; [ ] richer startup timeout/remediation and retry policy
- [ ] Configuration schema/version migration and stricter JSON parsing
- [ ] Installer-quality package tests on a clean Windows VM and a clean Linux container/VM
- [ ] Release artifact provenance/signing plan and verified update download staging

## 0.3 — hardware capability reporting

- [ ] Detect OS, architecture, CPU features, GPU vendor/device and driver version
- [ ] Report backend states as `detected`, `available`, `configured`, and `tested`
- [ ] CPU baseline and explicit fallback choices; never assume that a GPU is supported just because it exists
- [ ] Evidence-backed compatibility matrix from repeatable smoke tests
- [ ] RTX 3060 6 GB / 16 GB RAM test notes (the user's local test environment)

## 0.4 — engine/model catalog

The proposed separation and catalog fields are documented in [engine distribution strategy](engine-distribution.md).

- [x] Built-in catalog for official llama.cpp CPU/CUDA release assets and a pinned Qwen2.5 1.5B Q4_K_M GGUF starter model
- [x] License acknowledgement, HTTPS-only download, expected-size caps, SHA-256 validation, GGUF magic check and safe archive extraction
- [x] Download progress, cancellation, job status and user-data installation paths
- [ ] Resume interrupted downloads (current behavior cancels and discards partial files)
- [ ] Preserve user model/config directories across application updates (separate update installer not implemented)
- [x] First manual llama.cpp setup form, without silently executing downloaded scripts
- [ ] Probe newly installed engine binary and run a real local model health/chat test before marking it fully tested
- [ ] Automated hardware/backend detection and tested engine/model recommendations

## 0.5 — audio and multimodal adapters

- [ ] Add `audio.cpp` as a separate process adapter; begin with ASR, then TTS
- [ ] Evaluate `stable-diffusion.cpp`, ONNX Runtime, OpenCV/OCR and embedding/vector search
- [ ] Add each adapter only with its own test fixtures, capability manifest, license record and hardware notes

## Later — integrations and unattended updates

- [ ] OpenAI-compatible streaming endpoints and more task-specific APIs
- [ ] Optional MCP adapter with a narrow, audited tool allowlist
- [ ] Optional signed updater with staged download, signature/provenance verification, restart orchestration and rollback
- [ ] Linux/macOS distributable releases only after packaging and clean-machine smoke tests


### Platform CI policy

Windows x64 and Linux x64 are the current per-push build/package targets. On `main`, `release.yml` is the canonical build/test/package workflow; `ci.yml` runs on pull requests, non-main branch pushes and manual dispatch so a `main` commit is not compiled twice. Pages also filters pushes by `site/**`, since the website uses live GitHub Releases metadata and only needs redeployment when website files change. macOS support is not cancelled, but remains experimental and is checked by the separate weekly/manual `macos-compat.yml` workflow so Homebrew toolchain setup does not delay every developer push. A macOS release package will only be added after repeatable build, launch, and packaging checks are available.
