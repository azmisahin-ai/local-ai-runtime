# Next Session Handoff — Local AI Runtime

**Prepared:** 2026-11-01
**Repository:** <https://github.com/azmisahin-ai/local-ai-runtime>
**Purpose:** Finish the experimental Windows x64 audio.cpp/Qwen3-ASR pilot, then publish through the one canonical release process. Do not spend time on non-critical Chat/Markdown polish. The latest published release remains `v0.1.31-alpha`; all Stage 13 audio work described here is unreleased until the publisher confirms a new GitHub Release.

## Read these files first

1. `docs/roadmap.md` — current stage states and release gates.
2. `docs/stage-13-asr-windows-acceptance.md` — manual CPU/CUDA evidence and limits.
3. `docs/engine-integration-research.md` — upstream selection and pins.
4. `docs/engine-adapter-architecture.md` — keep Chat EngineAdapter separate from typed audio tasks.
5. `docs/engine-distribution.md` and `docs/catalog-authoring.md` — fail-closed installer/catalogue rules.

## Repository and release baseline

- Latest user-confirmed published baseline: `v0.1.31-alpha`, commit `e18a084`.
- User's Windows checkout was last confirmed at `e6155e6` with successful Windows Release build and 10/10 CTests. Subsequent working-tree changes in this handoff (requested-language fallback and five-sample test helper) are not yet merged into that checkout.
- Do not publish by pushing/tagging manually. Run `Publish-LocalAIRuntime.ps1` once from a clean, current `main`; it fast-forwards, waits for the single unified workflow, and verifies the tag/Release created from those same artifacts.
- Do not publish until the new language-fallback regression and five-sample script syntax check pass on Windows.

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

- CPU engine loaded the model (`configured=true`, `loaded=true`) and returned a non-empty transcript through `POST /v1/tasks/asr/transcribe` using `X-LAR-Language: tr`.
- Controlled seven-second Turkish WAV reference: `Bugün hava güzel. Türkçe konuşma tanıma testi yapıyorum.`
- CPU hypothesis: `Bugün hava güzel. Türkçe konuşmak tanımı testi yapıyorum.`; 2 substitutions / 8 reference words = 25% single-sample WER; latency 4.34 seconds.
- The same WAV on CUDA returned the same hypothesis in 4.79 seconds. `/health` reported `backend=cuda`; model was loaded. `nvidia-smi` listed the actual `audiocpp_server.exe` process during inference; system GPU memory was 1,723/6,144 MiB at 40% utilization. The per-process memory field was `N/A` under WDDM. After stop, GPU memory returned to 0 MiB and compute-process list was empty.
- This is a real experimental Windows x64 ASR pilot, not a broad Turkish quality benchmark. One scored sentence yielded 25% WER. The first exploratory 15-second clip was transcribed, but its exact spoken words were not independently confirmed and it must not be included in scored accuracy results.
- The real upstream response omitted its `language` field even though `tr` was sent. The new unreleased server change returns the explicitly requested language if the upstream omits it; this means “requested language”, not language detection. Windows Release/CTest must validate this regression.
- Real TTS has not been tested with a real TTS model. The fake HTTP TTS contract test is not evidence of usable TTS. Do not advertise TTS as supported in this release.

## Immediate release-preparation work

1. Merge the next bundle containing: requested-language fallback in `src/server.cpp`, an HTTP regression where fake audio.cpp omits `language`, and `scripts/Test-TurkishAsrAcceptance.ps1`.
2. Run `cmake --build build --config Release --parallel 4` and `ctest --test-dir build -C Release --output-on-failure` on Windows. Require all tests green, including the new language fallback smoke.
3. Run five controlled Turkish sentences with `scripts/Test-TurkishAsrAcceptance.ps1` against the CPU runtime. It records WAVs locally and reports per-utterance and aggregate WER. Then run the same WAV directory against CUDA using `-UseExistingAudioDirectory` so CPU/CUDA comparisons are fair.
4. Review the WER and reference/hypothesis pairs. No threshold was previously established; treat the result as an experimental quality record, not a guarantee. Do not commit WAV files or reports.
5. Verify docs, `git status --short`, and `git diff --check`; commit to `main`.
6. Publish exactly once with `Publish-LocalAIRuntime.ps1`. The automatic planner should select the next alpha after `v0.1.31-alpha`; don't hand-create a tag.
7. In release notes and README, describe the Windows audio.cpp/Qwen3-ASR pilot as experimental and state that TTS is not yet available.

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
