# Roadmap

## 0.1-alpha — first vertical slice

- [x] C++20/CMake project and local HTTP API
- [x] JSON engine manifest example
- [x] Start/stop an explicitly configured executable
- [x] Redirect engine stdout/stderr to local log files
- [x] Loopback-only status and engine control routes
- [x] Non-streaming chat proxy and `/v1/models` forwarding to the configured local OpenAI-compatible endpoint
- [x] Minimal same-origin web dashboard
- [x] Unit tests and Windows/Linux CI definition
- [ ] Verify build in Windows GitHub Actions and fix any toolchain differences
- [ ] Real llama-server smoke test on at least one CPU environment
- [ ] Real CUDA smoke test with exact GPU/driver/model recorded
- [ ] Capture startup health separately from OS process state

## 0.2-alpha — robustness

- Add process tests for success, crash, startup failure and timeouts.
- Implement bounded request concurrency, graceful server shutdown, and cancellation-safe child cleanup.
- Add bounded local upstream health probes and clearer startup/readiness states.
- Add streaming proxy support only after tests cover SSE cancellation and process shutdown.
- Add configuration schema/version migration and strict JSON validation.

## 0.3 — hardware capability reporting

- Detect OS/architecture and optional CPU/GPU information.
- Report backend capabilities as `detected`, `available`, `configured`, or `tested`.
- Keep CPU as the baseline; never silently download a backend.
- Add a hardware/backend test matrix sourced only from reproducible smoke tests.

## 0.4 — model registry and safe download

- Track source URL, format, version, license, size, SHA-256, architecture, and constraints.
- Add verified and cancellable downloads only after provenance and path-safety review.
- Keep engine licenses separate from model-weight licenses.

## 0.5+ — additional engines

- Add `audio.cpp` as an isolated adapter (ASR first, then TTS).
- Evaluate ONNX Runtime, stable-diffusion.cpp, OpenCV/OCR, embeddings/vector search based on demand.
- Add Linux/macOS release claims only after platform-specific CI and real engine smoke tests.
