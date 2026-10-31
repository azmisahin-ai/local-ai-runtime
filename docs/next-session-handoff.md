# Next Session Handoff — Local AI Runtime

**Prepared:** 2026-10-31  
**Repository:** <https://github.com/azmisahin-ai/local-ai-runtime>  
**Purpose:** Continue development in a fresh session without repeating the LLM/UI polishing loop. The next substantive work is audio-engine integration, beginning with audio.cpp.

## Read these files first

1. `docs/next-session-handoff.md` — current state and immediate task.
2. `docs/engine-integration-research.md` — dated engine selection, upstream findings, architecture gap and acceptance gates.
3. `docs/roadmap.md` — official stage order and open checks.
4. `docs/engine-adapter-architecture.md` — current EngineAdapter v1 contract and why audio.cpp needs a task adapter.
5. `docs/engine-distribution.md` and `docs/catalog-authoring.md` — installer, manifest and checksum constraints.

## Repository and release baseline

- Latest user-confirmed published baseline: **`v0.1.30-alpha`**, commit **`2307210`** (`fix(ui): normalize chat entities and strengthen locale prompts`).
- The user's Windows PowerShell 5.1 transcript confirmed that `Publish-LocalAIRuntime.ps1` pushed the commit and the unified main CI passed. Workflow: <https://github.com/azmisahin-ai/local-ai-runtime/actions/runs/41230378240>.
- Release: <https://github.com/azmisahin-ai/local-ai-runtime/releases/tag/v0.1.30-alpha>.
- `v0.1.30-alpha` publication is confirmed; do not assume the user has already installed and manually tested that exact build unless a later session confirms it.
- Routine release process remains build once / publish once. `Publish-LocalAIRuntime.ps1` pushes fast-forward-only `main`, waits for the single main CI, and reports release. Git bundles are only used for one-time source transfer; they are not part of normal publishing.
- At the last supplied `git status --short`, the user's local tree was clean before merging the `2307210` update. When resuming, verify repo branch and `git status --short`; never overwrite uncommitted user work.

## User's target hardware / local setup

- Windows PowerShell 5.1, Windows x64; exact Windows release/build not yet captured (`winver` needed before selecting WinML).
- NVIDIA GeForce RTX 3060 Laptop GPU, 6 GiB VRAM, PCI `10de:2520`, compute capability 8.6.
- NVIDIA driver 616.92; `nvidia-smi` available at `C:\Windows\System32\nvidia-smi.exe`.
- Intel UHD iGPU also present.
- 16 GB system RAM, 16 logical CPUs.
- Previous `llama.cpp` CUDA 13 runtime package: b12020; installed under `%LOCALAPPDATA%\LocalAIRuntime\engines\llama.cpp\b12020-cuda13`.
- Previous target model: Qwen3 4B Q4_K_M, pinned digest `7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5`.
- Previous Qwen3 CUDA runs showed about 85–91 generated tokens/s in llama.cpp timing lines. One Activity report measured system GPU memory/use while an engine was running, but Local AI Runtime still reported `gpu_execution_evidence: not-verified`, `cuda_runtime_reported:false`, and no offloaded-layer count. Keep system-wide NVIDIA telemetry separate from engine-specific offload evidence.

## What has been proven / what is still open

### LLM / Chat

- Qwen3 4B + llama.cpp CUDA 13 was downloaded, SHA-256 verified, profile-applied, engine-started and used for actual local inference.
- The `v0.1.27-alpha` UI initially showed an empty response even though the upstream engine logs showed token generation. Root cause was the UI's incomplete SSE field handling and success fallback.
- `v0.1.28-alpha` fixed the normal Qwen3 final-answer path; the user manually confirmed a visible response and a two-paragraph C++ AI article.
- `v0.1.29-alpha` added safe assistant Markdown rendering; user manually confirmed headings, emphasis, lists and C++ fenced code blocks render.
- `v0.1.30-alpha` adds bounded character-entity normalization and stronger locale instruction. The user confirmed publication, but no post-v0.1.30 manual acceptance has been supplied yet.
- Do not spend another iteration on minor Markdown, whitespace, inline-code or model-language style unless a concrete correctness/security defect appears. Basic Chat is good enough to move on.

### Hardware / UI residuals

- Stage 12 is not fully closed: app-side GPU layer-offload count/current-session CUDA evidence and KV-cache restore/context behavior remain open. This is follow-up work, not a blocker for audio integration.
- Stage 12.5 has shipped key onboarding, first-run flow, hardware evidence labels, curated text model shortlist, Qwen3 streaming correction and safe Markdown. Full manual responsive/accessibility/screen-reader review and v0.1.30 post-release visual check remain. These low-priority UI checks must not block Stage 13A.
- The catalog manifest currently supports `llama-release` and `pinned-gguf`; the latter is single-file GGUF only. Multi-file model directories and non-llama packages need a dedicated, fail-closed installer strategy.
- Existing `EngineAdapter` v1 exposes lifecycle/readiness and an upstream Chat completions path. It is not a generic task API. Do not register audio.cpp under `openai-compatible-process` or pretend that audio routes are Chat completions.

## Immediate next-session mission: Stage 13A — audio.cpp pilot

**First choice:** `0xShug0/audio.cpp` v0.9.3 (research checked 2026-10-31). Research and decisions are in `docs/engine-integration-research.md`.

### Why this is the first engine

- Pure C++/ggml runtime with ASR, TTS, VAD, audio enhancement/separation, voice conversion and music tasks.
- Official Windows CUDA runtime + profile package path exists.
- Published CUDA package prerequisites are compute capability >=7.5 and NVIDIA driver >=580; the user's 8.6 / 616.92 meets those prerequisites.
- Upstream server has health/models and audio task endpoints; this will require a real task adapter, not a Chat adapter.
- Qwen3-ASR-0.6B is listed with Turkish support; Chatterbox 0.5B multilingual TTS lists Turkish. Their model licenses are separate from the audio.cpp Apache-2.0 software license (Qwen3-ASR Apache-2.0; Chatterbox model MIT according to the model card checked 2026-10-31).

### First implementation sequence

1. Re-open official audio.cpp v0.9.3 release and Windows docs. Pin exact release tag/commit, exact asset names, sizes and SHA-256 digests. Do not install from floating `latest`.
2. Design the Local AI Runtime task-adapter contract. Keep `EngineAdapter` v1 and `llama.cpp` Chat stable. Add typed `asr.transcribe` / `tts.synthesize` operations, task capabilities, binary input staging/output artifact metadata, cancellation and process lifecycle. Do not make audio.cpp appear as a Chat model.
3. Evolve the embedded catalog/installer allowlist safely. The CUDA install is a pair: `audiocpp-windows-cuda-runtime.zip` plus `audiocpp-windows-cuda-balance.zip`; CPU fallback has `audiocpp-windows-cpu-balance.zip`. Model files are separate. Verify each asset and license; no unverified generic downloader or executing upstream installer scripts.
4. Build the narrowest Windows x64 CPU smoke first if practical: install, start `audiocpp_server`, `GET /health`, `GET /v1/models`, stop and restart. Then install the CUDA package pair and verify the same lifecycle.
5. Add Qwen3-ASR-0.6B as the first model. Use explicit language `tr` for Turkish; use a small, non-sensitive licensed WAV fixture and a human-reviewed reference transcript.
6. Run one CPU transcription, then one CUDA transcription, capturing task result, elapsed time/RTF, process RSS, `nvidia-smi` before/peak/after, and engine log evidence. Do not call GPU inference validated solely from system-wide telemetry.
7. Stop/restart audio.cpp before loading another model family if needed: upstream lazy-load keeps models resident until process exit. Do not keep llama.cpp, audio.cpp ASR and TTS concurrently loaded in 6 GB VRAM.
8. Only after ASR is accepted, add Chatterbox 0.5B TTS and generate a short Turkish WAV. Confirm actual output is playable and evaluate speech quality; record memory separately.
9. Compare whisper.cpp only after the first ASR path succeeds. Add sherpa-onnx only if it offers a measurable model/quality/compatibility advantage.
10. Update roadmap status and commit only after automated tests plus real Windows task tests. Build once and publish once through the canonical publisher.

### Required task acceptance record

For each task, record: runtime version, engine release/commit, archive names/sizes/SHA-256, model repo/revision/files/sizes/SHA-256/license, OS build, CPU/GPU/driver/VRAM, backend requested and reported, input fixture + reference, task success, text/audio output integrity, latency/RTF, peak RAM/RSS/VRAM, engine-specific offload evidence, and known limitations. Store only non-sensitive test fixtures/results in repository artifacts; do not place private user recordings, prompts or local filesystem paths in committed logs.

## Working style for the next session

- Focus on the next concrete engine and a runnable end-to-end task, not broad UI redesign.
- Prefer one engine family and one model at a time. Do not ask the user to install several experimental engines in one batch.
- Ask the user for a local PowerShell action only when it is needed to verify a real Windows package or task; provide exact PowerShell 5.1 commands.
- Preserve `%LOCALAPPDATA%\LocalAIRuntime` model/config data across runtime updates. Never request a destructive reset to debug engine packages.
- Keep engine packages, models and app release version independent.
- Update catalog/UI claims only after exact real-task evidence exists. Hosted CI remains a contract/build test, not target-hardware proof.
- The next task is **not** to polish Markdown or start MCP. It is to design and implement the audio.cpp task engine, beginning with a CPU lifecycle smoke and Qwen3-ASR Turkish task.

## Direct upstream links

- [audio.cpp v0.9.3 release](https://github.com/0xShug0/audio.cpp/releases/tag/v0.9.3)
- [audio.cpp Windows packages and requirements](https://github.com/0xShug0/audio.cpp/blob/main/docs/build/windows.md)
- [audio.cpp server API/model configuration](https://github.com/0xShug0/audio.cpp#server)
- [Qwen3-ASR-0.6B model card](https://huggingface.co/Qwen/Qwen3-ASR-0.6B)
- [Chatterbox multilingual model card](https://huggingface.co/ResembleAI/chatterbox)
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- [sherpa-onnx v2.0.1](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v2.0.1)
