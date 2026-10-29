# Roadmap

The roadmap distinguishes implemented code from platform/model combinations that have actually been tested. A build passing on GitHub Actions does not prove that CUDA, a particular GPU, or a particular model works.

## Active focus — Windows real-engine validation

The tagged Windows portable update flow and canonical four-platform package gate passed at commit `b764554`; `v0.1.0-alpha` was published successfully. Keep Windows as the primary user-facing platform while moving from fake-engine CI coverage to a repeatable, device-level real-inference check. The hosted CI intentionally does not download the engine or model weights.

- [x] Re-run Windows x64 CI after the `-ValidateOnly` parameter fix; validation mode checks a tampered archive without a runtime PID while actual apply mode still requires a positive PID.
- [x] Confirm the complete hosted Windows update smoke test passes: tamper rejection, package preflight, replacement/restart, user-data preservation, rollback, forced helper termination recovery, and retry after rollback failure.
- [x] Confirm all four platform package jobs pass and Pages deploys only after those jobs pass.
- [x] Publish the tag-gated `v0.1.0-alpha` prerelease with Windows/Linux/macOS Intel/macOS ARM64 archives and `SHA256SUMS.txt`.
- [x] Add a source-distributed Windows PowerShell real-engine smoke test that checks the configured `llama-server.exe`, GGUF header, model readiness, `/v1/models`, and a real non-streaming chat completion without downloading components or editing user config.
- [ ] Run `scripts\Test-LocalAIRuntimeRealEngine.ps1` on a real Windows 10/11 x64 machine with the starter model in CPU mode; preserve the local report and confirm user-data/configuration remain unchanged.
- [ ] Run a separate CUDA smoke test and record the GPU model, driver, VRAM, GGUF quantization, llama.cpp package/tag, and engine log evidence. A configured `--n-gpu-layers auto` value alone is not proof that CUDA is used.


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
- [ ] Real `llama-server` + GGUF smoke test on Windows and one CPU-only environment; the source PowerShell validation script is available at [real-engine validation](real-engine-validation.md), but a real device run is still required
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
- [x] Add a source-only local real-engine smoke-test script for Windows; user-device inference and CUDA validation remain separate checklist items
- [ ] Release artifact provenance/signing plan
- [x] Verified in-app update staging and tagged Windows portable apply/restart with checksum-verified rollback backup
- [x] Recover interrupted Windows file replacement on the next launcher start; re-verify backup SHA-256 values and retry recovery after a rollback failure

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
- [ ] Equivalent platform-native apply/recovery helpers and tests for Linux and macOS
- [ ] Release artifact provenance/signing plan; SHA-256 checks against release metadata are integrity checks, not publisher signatures
- [ ] Optional signed updater with staged download, signature/provenance verification, restart orchestration and rollback
- [ ] Linux/macOS distributable releases only after packaging and clean-machine smoke tests


### Platform CI policy

Windows x64, Linux x64, macOS Intel x64 and macOS Apple Silicon arm64 are coordinated by one `pipeline.yml`. Pull requests build and test each target without packaging. On `main`, the four platform/architecture targets build, test and package in parallel; the Pages deployment is the final job and runs only after all four extracted package smoke tests pass. A `v*` tag publishes all four assets with a SHA-256 manifest after the same gates pass. The weekly scheduled run executes only the macOS compatibility matrix, avoiding repeated Windows dependency compilation when no source push occurred.
