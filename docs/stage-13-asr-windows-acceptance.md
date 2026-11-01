# Stage 13 — Windows audio.cpp / Qwen3-ASR acceptance record

**Status:** Experimental real-task acceptance completed for CPU and CUDA on one Windows x64 machine. A broader five-utterance Turkish WER run remains a release-preparation check. TTS is not accepted or shipped by this record.

## Target environment

- OS: Windows x64; exact Windows edition/build was not captured.
- CPU: 16 logical processors; exact model not captured.
- RAM: 16 GiB.
- GPU: NVIDIA GeForce RTX 3060 Laptop GPU, 6 GiB VRAM, compute capability 8.6.
- NVIDIA driver: 616.92.
- Runtime source validated on Windows: `52af631` (Qwen3-ASR catalog/installer), then `e6155e6` (Windows atomic config replacement fix). The language-response fallback change and acceptance script are subsequent unreleased working-tree changes and still require Windows build/CTest.

## Pinned runtime and model artifacts

### audio.cpp v0.9.3

- Upstream version: `0.9.3`, upstream git `41c1f8ff`, date `2026-10-26`.
- CPU asset: `audio-v0.9.3-bin-windows-x64-cpu.zip`, `30,547,092` bytes, SHA-256 `3ac66f46395b4e92e4f3bb68b200f0032310bd9922f12d97f10ccdadb02dee4b`.
- CUDA server: `audio-v0.9.3-bin-windows-x64-cuda13.3.zip`, `277,336,614` bytes, SHA-256 `0bd3988be10c31ec078c40df2bd0d4266e660804f6d5083aeb05d1309d091e43`.
- CUDA runtime: `audio-v0.9.3-cudart-windows-x64-cuda13.3.zip`, `575,457,454` bytes, SHA-256 `839eb20f684a8aab64895b1e466b32ce9b29dfc19dc1ce15b2ef35218512ac0a`.
- Software license: Apache-2.0; the user explicitly accepted it for CPU and CUDA package installs.

### Qwen3-ASR 0.6B Q8_0

- Model ID: `qwen3-asr-0.6b-q8_0`.
- Repository/revision: `audio-cpp/audio.cpp-gguf`, commit `4c9a3098d552a0f3d9b2adc11638a11022c07d7e`.
- File: `Qwen3-ASR-0.6B-GGUF/qwen3-asr-0.6b-q8_0.gguf`.
- Size: `1,151,272,416` bytes.
- SHA-256: `6c44ec2fb4cee513892d7863c1fcc3ea6b699ffa4d899b0ef4ab19956d9544f7`.
- Model license: Apache-2.0; the user explicitly accepted it separately from the audio.cpp software license.
- Managed config registration: `family=qwen3_asr`, `task=asr`, `mode=offline`; model is not a Chat model.

## Windows installation and lifecycle

- CPU package installed using the Local AI Runtime catalog installer in an isolated data root. Exact archive size/hash, safe extraction, engine registration, `/health`, `/v1/models`, start, stop, restart, process exit and listener cleanup were verified.
- CUDA 13.3 server+CUDART pair installed using the catalog installer in a separate isolated data root. Aggregate bytes: `852,794,068`. The binary reported CPU and CUDA backends. Managed readiness and `/health` returned `backend=cuda`; stop removed the process and listener.
- Qwen3-ASR model downloaded once into the CPU test root. The first install attempt failed only during server-config replacement with Windows error 5; the fully downloaded model file was independently verified at exact size/hash. After commit `e6155e6` closed the config input stream before atomic replacement, a retry reused the existing verified model and completed `Installed and configured`.
- The same model file was copied locally to the CUDA test data root; its byte size and SHA-256 were verified before the managed installer recognized and registered the existing file. No second 1.15 GB network download was needed.

## Real transcription observations

All requests used the Local AI Runtime typed endpoint `POST /v1/tasks/asr/transcribe`, model ID `qwen3-asr-0.6b-q8_0`, and requested language `tr`. Both CPU and CUDA used the same second WAV fixture for a direct comparison.

### CPU — first, exploratory recording

- WAV: 16 kHz mono, 16-bit PCM; 479,662 bytes; 15 seconds.
- Latency: 9.63 seconds (RTF approximately 0.64).
- audio.cpp process working set: 12.7 MiB before, 2,042.4 MiB after.
- A non-empty Turkish transcript was returned. The exact spoken utterance was not independently confirmed word-for-word, so this sample is not included in the formal WER observation.

### CPU — controlled short phrase

- Reference: `Bugün hava güzel. Türkçe konuşma tanıma testi yapıyorum.`
- WAV: 16 kHz mono, 16-bit PCM; 223,982 bytes; 7 seconds.
- Hypothesis: `Bugün hava güzel. Türkçe konuşmak tanımı testi yapıyorum.`
- Latency: 4.34 seconds (RTF approximately 0.62).
- Word-level errors after Turkish-locale lowercase and punctuation normalization: 2 substitutions / 8 reference words; observed single-utterance WER: **25%**.

### CUDA — same controlled short phrase

- Same 7-second, 223,982-byte WAV and same reference text as the CPU run.
- Backend health: `status=ok`, `backend=cuda`; model registry showed `loaded=true` after inference.
- Hypothesis: identical to CPU: `Bugün hava güzel. Türkçe konuşmak tanımı testi yapıyorum.`
- Latency: 4.79 seconds (RTF approximately 0.68).
- audio.cpp process working set: 19.7 MiB before, 739.9 MiB after.
- `nvidia-smi` after inference: 1,723 / 6,144 MiB total GPU memory in use and 40% utilization. NVIDIA compute-process query listed the actual `audiocpp_server.exe` PID `1480`; per-process GPU memory was reported as `N/A` by the driver/WDDM view. After engine stop, process and listener were absent and GPU memory returned to 0 MiB.

## Interpretation and limits

- Real ASR requests succeeded through the Local AI Runtime task adapter on both CPU and CUDA. The same fixture produced identical text on both backends.
- CUDA execution is evidenced by the `audiocpp_server.exe` compute-process entry and contemporaneous system GPU utilization. The reported 1,723 MiB is system-wide GPU memory use, not a process-specific allocation measurement.
- The one controlled 8-word fixture had 25% WER. This is not enough data to claim general Turkish accuracy or production-grade transcription. Publish the ASR integration as an **experimental Windows x64 pilot**, with users advised to review transcripts.
- The real audio.cpp response omitted its `language` property despite the request including `language=tr`. The task adapter passed `tr` upstream; the current unreleased server change will fall back to the explicitly requested language in Local AI Runtime's response when upstream omits it. This fallback means “requested language”, not language detection.
- TTS has not been tested with a real TTS model. The typed TTS contract/fake HTTP smoke is not evidence of real TTS support. Do not advertise TTS as available in this release.
- The five-sample acceptance helper `scripts/Test-TurkishAsrAcceptance.ps1` records WAV fixtures locally, computes word-level WER (Turkish casing, punctuation removal and circumflex-variant normalization) and emits a JSON summary. It also checks the normalized response preserves the explicit `tr` request. Use identical WAVs on both backends; do not commit recordings or reports.

## Release gate

Before publishing the next alpha:

1. Run the five-sample helper against CPU and CUDA using the same recorded WAV directory; inspect the reference/hypothesis pairs and WER. Do not use the exploratory first recording as a scored sample.
2. Build the current working tree on Windows Release and run all CTests (including the response-language fallback HTTP regression).
3. Confirm the working tree is clean and the commit is on `main`.
4. Publish exactly once via `Publish-LocalAIRuntime.ps1`; let its single unified workflow build, test, package and create the tag/Release from the same artifacts.

TTS and ASR-engine alternatives remain separate follow-up stages.
