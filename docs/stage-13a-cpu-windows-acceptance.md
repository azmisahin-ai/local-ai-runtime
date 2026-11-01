# Stage 13A — Windows x64 CPU Acceptance Record

**Acceptance date:** 2026-11-01  
**Runtime source commit tested:** `3103b45` (`fix(diagnostics): normalize invalid UTF-8 before export`)  
**Audio engine:** `0xShug0/audio.cpp` v0.9.3, upstream Git `41c1f8ff`  
**Target:** Windows x64, MSVC Release build, CPU package

## 1. Native upstream package

- Asset: `audio-v0.9.3-bin-windows-x64-cpu.zip`
- Exact size: `30,547,092` bytes
- SHA-256: `3ac66f46395b4e92e4f3bb68b200f0032310bd9922f12d97f10ccdadb02dee4b`
- Archive size check: passed
- SHA-256 check: passed
- Safe extraction into temporary smoke directory: passed
- Binary identity: `audio.cpp 0.9.3`, Git `41c1f8ff`, Release / MSVC / Windows AMD64
- Compiled backend list: `cpu`

## 2. Native process lifecycle

- API-only config with `backend=cpu`, `ui_management=true`, `lazy_load=true`, `max_loaded_models=1` and an empty `models` array was accepted.
- Server listened on loopback port `18082`.
- `GET /health`: `status=ok`, `backend=cpu`, `models=0`, `version=0.9.3`.
- `GET /v1/models`: object `list`, empty `data` array.
- Second server start succeeded.
- Final executable-process query was empty after stop.
- Final TCP listener query for port `18082` was empty.

## 3. Local AI Runtime managed installation

- Runtime Release executable started with an isolated temporary data root; the normal `%LOCALAPPDATA%\LocalAIRuntime` data was not used.
- Catalog item: `audio.cpp-v0.9.3-cpu`.
- Catalog exact byte size and SHA-256 matched the reviewed release pins.
- License acceptance was supplied for the Apache-2.0 engine package.
- Installer job: `download-1`; terminal state: `succeeded`.
- Downloaded bytes: `30,547,092 / 30,547,092`.
- Safe extraction, staging and atomic activation: passed.
- Installed binary `--version`: v0.9.3 / Windows AMD64 / CPU backend.
- Generated server config: `backend=cpu`, `ui_management=true`.
- Catalog installed state: `true`.
- Engine registration: adapter `audio.cpp`, engine status initially `stopped`, `default_for_chat=false`.

## 4. Managed process lifecycle

- Managed start: passed; process became `ready=true`.
- Upstream `GET /health`: `status=ok`, `backend=cpu`, `models=0`, `version=0.9.3`.
- Upstream `GET /v1/models`: empty model list, as expected for an engine-only package.
- First managed stop: passed; old PID disappeared and engine listener closed.
- Managed restart: passed with a new PID; health remained `ok / cpu / 0.9.3`.
- Final managed stop: passed; engine state `stopped`, PID `0`.
- Local AI Runtime parent process remained running during the engine stop/restart test.

## 5. Windows build and automated tests

- Release build: passed.
- Targeted `lar_http_smoke`: passed after diagnostics UTF-8 normalization.
- Full CTest suite: **10/10 passed**.
- The test fix converts invalid UTF-8 in diagnostics text to valid replacement characters before export; this is independent of audio.cpp inference.

## 6. Scope and limitations

This record accepts the Windows x64 CPU **engine package installation and managed lifecycle only**. It does not claim that ASR or TTS model weights have been installed, that a real transcription or synthesis task has run, or that CUDA works. The CPU package is the portable/testing profile and is not a CPU performance claim.

Next acceptance: exact-pinned Windows CUDA 13.3 server + CUDART package pair, installed into a separate isolated runtime data root, followed by native version/backend, CPU fallback and managed CUDA process lifecycle checks. Then pin Qwen3-ASR-0.6B model files and run the same reviewed Turkish WAV/reference transcript on CPU and CUDA.
