# Shared Engine Adapter Architecture

## Contract version

The current C++ adapter contract is version `1`, exposed as `adapter_contract_version` in engine status. Adapter IDs are stable implementation identifiers, not package versions. Incompatible lifecycle or protocol changes require a new adapter contract version and explicit migration tests.

## Purpose

The engine adapter is the lifecycle/protocol boundary for a managed local inference server. `EngineAdapter` v1 remains the Chat contract used by llama.cpp; Stage 13A adds the separate typed `TaskEngineAdapter` for audio.cpp tasks. This does not claim Ollama, ONNX Runtime or other media engines are implemented.

## Responsibilities

Each adapter declares a stable `adapter_id` and owns:

- validation of the engine-specific configuration before process launch;
- start/stop/status policy, delegating OS process ownership to `ManagedProcess` by default;
- process identity and exit-state inspection, which can be overridden for service-backed adapters;
- primary and optional fallback readiness endpoints;
- for Chat adapters, the upstream chat-completion endpoint used by the normalized Chat API. Non-Chat task protocols are represented by the separate `TaskEngineAdapter` contract below.

The runtime continues to expose its same-origin `/v1/chat/completions` contract for Chat engines. `TaskEngineAdapter` owns typed non-Chat request/response translation; lifecycle, Activity persistence, resource telemetry and user-data ownership remain in shared runtime services.

## Registered adapters in this milestone

| Adapter ID | Purpose | State |
|---|---|---|
| `llama.cpp` | Official `llama-server` process, GGUF model validation, `/health` with `/v1/models` fallback | Implemented first-party adapter |
| `audio.cpp` | Managed audio.cpp server process, `/health` with `/v1/models` fallback; no Chat completions route | Stage 13A lifecycle adapter; native Windows package validation pending |
| `openai-compatible-process` | Generic local process using an OpenAI-compatible `/v1/models` and `/v1/chat/completions` API | Compatibility base used by test/custom process configurations; not a separately cataloged engine product |

Unknown adapter IDs are rejected. There is no arbitrary shell adapter and no user-supplied executable or remote URL is automatically trusted. Existing configs without `adapter_id` are migrated deterministically: `llama-cpp` maps to `llama.cpp`; other existing process configs map to `openai-compatible-process`.

## Configuration

New configurations persist `adapter_id` on each engine entry. The field is additive and older config files remain readable. The example config explicitly uses `llama.cpp`. The adapter ID is validated when loading/saving config, constructing `EngineManager`, and replacing engine configurations.

## Deferred work

The following are not part of this milestone:

- catalog installers for Ollama, ONNX Runtime, Whisper, image/video or music engines;
- additional typed task-family protocols beyond the Stage 13A audio.cpp ASR/TTS adapter;
- model-format-specific loaders beyond the existing llama.cpp GGUF path;
- hardware compatibility evidence and automatic backend selection.

A future adapter that uses a different wire protocol must add an explicit request/response translation contract and tests. It must not silently masquerade as an OpenAI-compatible process. Each new adapter also requires manifest metadata, dependency/install strategy, platform packaging, lifecycle tests and real-task evidence before being shown as supported.

## Typed task adapters (Stage 13A implementation)

The v1 `EngineAdapter` is intentionally Chat-oriented: it exposes a readiness path and an upstream Chat completions path. It is not a general-purpose multimodal/task protocol. Do not register ASR, TTS, image generation, OCR, embeddings, video or 3D tools as `openai-compatible-process` merely because the executable can be started.

Stage 13A introduced a separate `TaskEngineAdapter` contract in `include/lar/task_adapter.hpp`; `audio.cpp` is registered as a managed process adapter while `EngineAdapter` v1 stays Chat-only. The typed operations are `asr.transcribe` (bounded WAV input, multipart upstream mapping, transcript result) and `tts.synthesize` (bounded JSON input, WAV artifact result). They are exposed through `POST /v1/tasks/asr/transcribe` and `POST /v1/tasks/tts/synthesize`. ASR accepts raw `audio/wav` bytes and the model/language identifiers in `X-LAR-Model` and `X-LAR-Language`; it does not accept a filesystem path. TTS accepts `model_id`, `text`, and optional `voice` JSON fields and responds with `audio/wav`. The current adapter advertises task cancellation as unsupported; the blocking upstream proxy does not yet provide per-task cancellation.

Process lifecycle uses `/health` and `/v1/models`. Activity records an `audio-task` operation but never includes audio bytes, transcript text, TTS text or local paths in diagnostics. Request bounds are 24 MiB WAV input/output and 64 KiB TTS JSON. `audio.cpp` is never registered as a default Chat engine and does not expose a Chat completions path.

The compiled catalog accepts `audio-cpp-release-set` only for exact v0.9.3 Windows x64 pins: one CPU server archive or a paired CUDA 13.3 server+CUDART archive. Each asset requires a fixed official URL, filename, SHA-256 and exact positive byte size. The installer downloads into per-user staging, checks size and hash, rejects unsafe archive entries, merges only regular files/directories, atomically activates and rolls back if config registration fails. The server config is written in staging before activation and uses a loopback-selected ephemeral port rather than a hard-coded listener port. Exact byte sizes have been confirmed from the official GitHub release API and are enforced in the compiled allowlist with SHA-256; no audio.cpp catalog entry is active until native Windows CPU installer/lifecycle validation passes. This strategy does not yet install model assets.

Hosted tests cover a fake-engine CPU lifecycle, normalized task HTTP behavior and CPU/CUDA package-set allowlist fixtures. They are contract tests, not proof that the native audio.cpp package starts on Windows or that CUDA/model inference works. Those target-machine gates remain open.
