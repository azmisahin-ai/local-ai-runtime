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
- [x] Cross-platform CI definition: Windows/MSVC, Ubuntu/GCC, macOS/Clang
- [x] Project Pages site and feature manifest
- [x] Tag-based Windows/Linux release workflow with SHA-256 manifest
- [x] Confirm hosted Windows/MSVC CI on the public repository (green at commit `1f020c4`)
- [x] Confirm hosted macOS CI on the public repository (green at commit `1f020c4`)
- [ ] Test the release workflow by publishing a prerelease tag after the packaged-archive health checks pass
- [ ] Real `llama-server` smoke test on Windows and one CPU-only environment
- [ ] Real CUDA smoke test with GPU model, VRAM, driver, model quantization and backend recorded
- [ ] Startup readiness probe separated from process-running state
- [x] Add extracted Windows/Linux archive checks for version, dashboard/launcher files, and a live `/health` response in the release workflow (awaiting first tag-triggered run)

## 0.2-alpha — product reliability

- [ ] Bounded request concurrency, graceful HTTP shutdown and cancellation-safe child cleanup
- [ ] Windows Job Object process cleanup and graceful stop policy
- [ ] Engine readiness probes, startup timeout and useful remediation messages
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

- [ ] Model registry with source, format, model/license metadata, size, checksum and system requirements
- [ ] Cancellable and resumable model downloads with source allowlisting, path validation and SHA-256 verification
- [ ] Preserve user model/config directories across application updates
- [ ] User-friendly engine discovery and setup, without silently executing downloaded scripts

## 0.5 — audio and multimodal adapters

- [ ] Add `audio.cpp` as a separate process adapter; begin with ASR, then TTS
- [ ] Evaluate `stable-diffusion.cpp`, ONNX Runtime, OpenCV/OCR and embedding/vector search
- [ ] Add each adapter only with its own test fixtures, capability manifest, license record and hardware notes

## Later — integrations and unattended updates

- [ ] OpenAI-compatible streaming endpoints and more task-specific APIs
- [ ] Optional MCP adapter with a narrow, audited tool allowlist
- [ ] Optional signed updater with staged download, signature/provenance verification, restart orchestration and rollback
- [ ] Linux/macOS distributable releases only after packaging and clean-machine smoke tests
