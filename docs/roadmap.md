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
- [x] In-app platform update package staging with official-release URL allowlist, size bound, and SHA-256 verification (does not install packages)
- [x] one canonical pipeline validates pull requests, builds/tests/packages Windows x64, Linux x64, macOS Intel and macOS Apple Silicon on `main`, then deploys Pages as its final job only after all four target jobs pass; the weekly scheduled run executes only the macOS compatibility matrix
- [x] Project Pages site and feature manifest
- [x] Tag-based Windows/Linux/macOS-x64/macOS-arm64 release workflow with a SHA-256 manifest
- [x] Confirm hosted Windows/MSVC CI on the public repository (green at commit `1f020c4`)
- [x] Confirmed macOS CI passed on the native Intel runner; the canonical workflow now checks Intel and Apple Silicon in parallel and requires both before Pages/Release
- [x] Publish the first prerelease tag after user-facing llama.cpp setup and four-platform package CI passed
- [ ] Real `llama-server` + GGUF smoke test on Windows and one CPU-only environment; CI currently uses a fake engine and intentionally does not fetch external components
- [x] Dashboard setup form for an existing local `llama-server` and GGUF file; persists the engine configuration without hand-editing JSON
- [x] Runtime readiness state from the local `/health` endpoint (fallback to `/v1/models`) and chat gating in the dashboard
- [ ] Real CUDA smoke test with GPU model, VRAM, driver, model quantization and backend recorded
- [x] Startup readiness probe separated from process-running state (runtime health signal; real model loading still needs a real-engine smoke test)
- [x] Add extracted Windows/Linux/macOS archive checks for version, dashboard/launcher files, and a live `/health` response in the release workflow
- [x] Auto-build Windows/Linux/macOS x64 and arm64 preview artifacts on each `main` push; public releases remain tag-gated
- [x] Surface preview builds from the website while no public release exists

## 0.2-alpha — product reliability

- [ ] Bounded request concurrency, graceful HTTP shutdown and cancellation-safe child cleanup
- [ ] Windows Job Object process cleanup and graceful stop policy
- [x] Basic engine readiness probe and dashboard chat gating; [ ] richer startup timeout/remediation and retry policy
- [ ] Configuration schema/version migration and stricter JSON parsing
- [ ] Installer-quality package tests on a clean Windows VM and a clean Linux container/VM
- [ ] Release artifact provenance/signing plan
- [x] Verified in-app update package staging with exact release asset allowlist, size bound and SHA-256 check (application/rollback remains unimplemented)

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

Windows x64, Linux x64, macOS Intel x64 and macOS Apple Silicon arm64 are coordinated by one `pipeline.yml`. Pull requests build and test each target without packaging. On `main`, the four platform/architecture targets build, test and package in parallel; the Pages deployment is the final job and runs only after all four extracted package smoke tests pass. A `v*` tag publishes all four assets with a SHA-256 manifest after the same gates pass. The weekly scheduled run executes only the macOS compatibility matrix, avoiding repeated Windows dependency compilation when no source push occurred.
