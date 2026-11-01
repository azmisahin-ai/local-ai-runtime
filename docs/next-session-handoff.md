# Next Session Handoff — Local AI Runtime

**Prepared:** 2026-11-01
**Repository:** <https://github.com/azmisahin-ai/local-ai-runtime>
**Purpose:** Complete final documentation review for the experimental Windows x64 audio.cpp/Qwen3-ASR pilot, then publish through the one canonical release process. Do not spend time on non-critical Chat/Markdown polish. The latest published release remains `v0.1.31-alpha`; Stage 13 audio work remains unreleased until the publisher confirms a new GitHub Release.

## Read these files first

1. `docs/roadmap.md` — current stage states and release gates.
2. `docs/stage-13-asr-windows-acceptance.md` — manual CPU/CUDA evidence and limits.
3. `docs/engine-integration-research.md` — upstream selection and pins.
4. `docs/engine-adapter-architecture.md` — keep Chat EngineAdapter separate from typed audio tasks.
5. `docs/engine-distribution.md` and `docs/catalog-authoring.md` — fail-closed installer/catalogue rules.

## Repository and release baseline

- Latest user-confirmed published baseline: `v0.1.31-alpha`, commit `e18a084`.
- User's Windows `main` contains `db0cd00` plus `4d9dad5`; Windows Release build/CTest passed 10/10 on `db0cd00`. After `4d9dad5`, the Windows PowerShell 5.1 acceptance script was syntax-checked and run successfully with the BOM fix.
- CPU/CUDA five-sample acceptance is complete. Results and limits are recorded below and in `docs/stage-13-asr-windows-acceptance.md`.
- Do not publish by pushing/tagging manually. Run the canonical `scripts/Publish-LocalAIRuntime.ps1` once from a clean, current `main`; it fast-forwards, waits for the single unified workflow, and verifies the tag/Release created from those same artifacts.

## User's target machine

- Windows x64, PowerShell 5.1.
- NVIDIA GeForce RTX 3060 Laptop GPU, 6 GiB VRAM, compute capability 8.6, driver 616.92.
- 16 GiB system RAM and 16 logical CPU processors.
- Exact Windows edition/build and CPU model have not been captured.

## Verified Windows evidence

### audio.cpp v0.9.3 runtime

- CPU package installed and managed in the isolated data root `%TEMP%\LAR-Stage13A-CPU-1035bd5851c448fc813dd02e02071c21` (runtime port 64702).
- CUDA 13.3 server+CUDART installed and managed in `%TEMP%\LAR-Stage13A-CUDA-f95fccefc0a048d0a1389236b7dc1c54`.
- Both managed engines passed readiness and `/health`; final stop removed the engine process and listening port. CPU and CUDA tests used isolated roots because a managed install must not overwrite an existing engine component.
- Package pins are recorded in `docs/stage-13-asr-windows-acceptance.md`.

### Qwen3-ASR model

- Model ID `qwen3-asr-0.6b-q8_0`, Apache-2.0 (the user explicitly accepted the model license separately from audio.cpp's Apache-2.0 license).
- Exact file size `1,151,272,416` bytes; SHA-256 `6c44ec2fb4cee513892d7863c1fcc3ea6b699ffa4d899b0ef4ab19956d9544f7`.
- CPU installation hit Windows error 5 while atomically replacing audio.cpp config. Commit `e6155e6` fixed it by closing the config input stream before replace. A retry reused the existing verified model and registered `family=qwen3_asr`, `task=asr`, `mode=offline`.
- Model was copied to CUDA test root, size/hash reverified, and the managed installer recognized the already-installed model and registered it without a second network download.

### Real CPU and CUDA ASR

- Five 8-second, mono, 16 kHz, 16-bit PCM WAV fixtures were captured once and reused unchanged on CPU and CUDA.
- Overall WER: CPU **41.38% (12/29 word edits)**; CUDA **37.93% (11/29)**.
- Mean request latency: CPU **4.31 s**; CUDA **0.84 s**. The first CUDA request took 3.00 s; its remaining four averaged 0.31 s. This is a single-device observation, not a portable performance guarantee.
- Per-sample WER: sample 01 0% on both; sample 02 CPU 40%, CUDA 20%; sample 03 83.3% on both; sample 04 42.9% on both; sample 05 25% on both. Sample 03 had a severe recognition error. Sample 04's spoken-number versus numeric-unit spelling (`on altı kilohertz` / `16 kHz`) is scored as a surface-text difference by the current WER helper.
- CUDA readiness health returned `status=ok`, `backend=cuda`. The earlier direct inference test also showed `audiocpp_server.exe` in NVIDIA's compute process list; system GPU memory was 1,723/6,144 MiB at 40% utilization during that earlier test, and returned to 0 MiB after engine stop. WDDM did not expose process-specific GPU memory.
- Treat this as an experimental Windows x64 integration pilot, not a broad Turkish quality benchmark. The first exploratory 15-second clip remains excluded because its exact reference was not independently confirmed. Users must review transcripts.
- The upstream response omitted `language` even when `tr` was sent. Commit `db0cd00` returns the explicitly requested language if upstream omits it; this means “requested language”, not language detection. Windows Release/CTest passed 10/10 on that commit.
- Commit `4d9dad5` adds a UTF-8 BOM to the acceptance helper for Windows PowerShell 5.1; its syntax check and five-sample rerun passed. Real TTS has not been tested with a real model and must not be advertised.

## Immediate release-preparation work

1. Review the updated acceptance record, README, roadmap, product manifest and changelog so the five-sample results and experimental limitations agree.
2. Run `git diff --check`, review the documentation diff and commit the updates to `main`.
3. Confirm `git status --short` is empty. The canonical publisher will verify that local `main` is a fast-forward of official `origin/main` before sending anything.
4. Publish exactly once with `scripts/Publish-LocalAIRuntime.ps1`. Do not run `git push`, create tags, or launch a second workflow manually. The publisher selects the next alpha and waits for the single build-once workflow.
5. Review the publisher's final Release and Actions URLs. Keep TTS explicitly unavailable and Turkish ASR labelled experimental; do not claim general accuracy from five utterances.

## Five-sample acceptance helper

The new helper accepts a loopback runtime URL and requires `audio-cpp` to be Ready with the Qwen model configured. Example CPU run:

```powershell
& .\scripts\Test-TurkishAsrAcceptance.ps1 -BaseUrl $cpuBaseUrl -OutputDirectory "$env:TEMP\lar-asr-cpu"
```

Use the WAVs it recorded for a matching CUDA run:

```powershell
& .\scripts\Test-TurkishAsrAcceptance.ps1 -BaseUrl $cudaBaseUrl -OutputDirectory "$env:TEMP\lar-asr-cuda" -UseExistingAudioDirectory "$env:TEMP\lar-asr-cpu\audio"
```

The script calls only the supplied `127.0.0.1` runtime URL, writes its report locally, normalizes text using Turkish casing, computes word-level edit distance/WER, and never uploads test audio to a remote service. Do not commit the WAV files or reports.

## Later work — not release blockers for the ASR pilot

- Real Turkish TTS: pin Chatterbox (or another justified model), license/revision/files/hashes, then test playable output on CPU and CUDA with VRAM measured. Do not infer TTS support from the task route or fake engine.
- ASR alternatives: compare whisper.cpp only after the initial ASR pilot ships; use sherpa-onnx only if it offers a measurable advantage.
- Stage 12 app-side GPU layer evidence, KV-cache/context restore, full accessibility review, signing/provenance and Linux/macOS update parity remain tracked separately.

## Direct upstream links

- [audio.cpp v0.9.3 release](https://github.com/0xShug0/audio.cpp/releases/tag/v0.9.3)
- [audio.cpp Windows packages and requirements](https://github.com/0xShug0/audio.cpp/blob/main/docs/build/windows.md)
- [audio.cpp server API/model configuration](https://github.com/0xShug0/audio.cpp#server)
- [Qwen3-ASR-0.6B model card](https://huggingface.co/Qwen/Qwen3-ASR-0.6B)
- [Pinned Qwen3-ASR GGUF file](https://huggingface.co/audio-cpp/audio.cpp-gguf/blob/4c9a3098d552a0f3d9b2adc11638a11022c07d7e/Qwen3-ASR-0.6B-GGUF/qwen3-asr-0.6b-q8_0.gguf)
- [Chatterbox multilingual model card](https://huggingface.co/ResembleAI/chatterbox)
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp)
- [sherpa-onnx v2.0.1](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v2.0.1)
