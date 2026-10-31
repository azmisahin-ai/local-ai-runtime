# Shared Engine Adapter Architecture

## Contract version

The current C++ adapter contract is version `1`, exposed as `adapter_contract_version` in engine status. Adapter IDs are stable implementation identifiers, not package versions. Incompatible lifecycle or protocol changes require a new adapter contract version and explicit migration tests.

## Purpose

The engine adapter is the boundary between Local AI Runtime's task-oriented API and a managed local inference server. The first implementation keeps llama.cpp as the only first-party managed engine. This milestone creates the contract and registry; it does not claim Ollama, ONNX Runtime, or media engines are implemented.

## Responsibilities

Each adapter declares a stable `adapter_id` and owns:

- validation of the engine-specific configuration before process launch;
- start/stop/status policy, delegating OS process ownership to `ManagedProcess` by default;
- process identity and exit-state inspection, which can be overridden for service-backed adapters;
- primary and optional fallback readiness endpoints;
- the upstream chat-completion endpoint used by the normalized runtime API.

The runtime continues to expose its same-origin `/v1/chat/completions` contract. The selected adapter determines the upstream readiness and completion paths. Streaming, cancellation, Activity persistence, resource telemetry and user-data ownership remain in the shared runtime services.

## Registered adapters in this milestone

| Adapter ID | Purpose | State |
|---|---|---|
| `llama.cpp` | Official `llama-server` process, GGUF model validation, `/health` with `/v1/models` fallback | Implemented first-party adapter |
| `openai-compatible-process` | Generic local process using an OpenAI-compatible `/v1/models` and `/v1/chat/completions` API | Compatibility base used by test/custom process configurations; not a separately cataloged engine product |

Unknown adapter IDs are rejected. There is no arbitrary shell adapter and no user-supplied executable or remote URL is automatically trusted. Existing configs without `adapter_id` are migrated deterministically: `llama-cpp` maps to `llama.cpp`; other existing process configs map to `openai-compatible-process`.

## Configuration

New configurations persist `adapter_id` on each engine entry. The field is additive and older config files remain readable. The example config explicitly uses `llama.cpp`. The adapter ID is validated when loading/saving config, constructing `EngineManager`, and replacing engine configurations.

## Deferred work

The following are not part of this milestone:

- catalog installers for Ollama, ONNX Runtime, Whisper, image/video or music engines;
- native request/response translation for non-OpenAI protocols;
- model-format-specific loaders beyond the existing llama.cpp GGUF path;
- hardware compatibility evidence and automatic backend selection.

A future adapter that uses a different wire protocol must add an explicit request/response translation contract and tests. It must not silently masquerade as an OpenAI-compatible process. Each new adapter also requires manifest metadata, dependency/install strategy, platform packaging, lifecycle tests and real-task evidence before being shown as supported.

## Future typed task adapters (planned; not implemented)

The v1 `EngineAdapter` is intentionally Chat-oriented: it exposes a readiness path and an upstream Chat completions path. It is not a general-purpose multimodal/task protocol. Do not register ASR, TTS, image generation, OCR, embeddings, video or 3D tools as `openai-compatible-process` merely because the executable can be started.

Stage 13A (see [engine integration research](engine-integration-research.md)) will design a separate typed task-adapter boundary or an explicitly versioned successor contract. The first candidate is `audio.cpp`, whose server exposes `GET /health`, `GET /v1/models`, `POST /v1/audio/transcriptions`, `POST /v1/audio/speech`, and `POST /v1/tasks/run`. Its routes and binary audio request/response semantics do not match `/v1/chat/completions`.

The task contract must declare stable engine ID, task capabilities (initially `asr.transcribe` and `tts.synthesize`), config validation, process lifecycle, readiness, typed request/response mapping, bounded local file staging, output-artifact metadata, cancellation, per-task limits and engine-specific evidence. Keep prompt/audio content out of diagnostics by default. Reuse the existing managed-process, FIFO job queue, Activity and telemetry services only where their semantics remain correct.

The catalog and installer must evolve independently and fail closed. Current manifest-v1 strategies (`llama-release` and `pinned-gguf`) do not describe an audio.cpp runtime archive pair or a multi-file model package. Add a reviewed installer strategy and/or a versioned manifest contract only after exact upstream release assets, model files, sizes, SHA-256 values, licenses and extraction behavior have been verified. No arbitrary URL, command or upstream model-manager script is permitted.

This section is an architecture decision and work plan, not a claim that task adapters or audio.cpp support already exist.
