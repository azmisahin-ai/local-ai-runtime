# Engine Integration Research and Selection

**Research checked:** 2026-11-01
**Target machine:** Windows x64, NVIDIA GeForce RTX 3060 Laptop GPU (6 GB VRAM), 16 GB system RAM, 16 logical CPUs, NVIDIA driver 616.92, compute capability 8.6.
**Decision status:** `audio.cpp` v0.9.3 is integrated as a non-Chat task runtime; the managed Windows x64 CPU and CUDA 13.3 package lifecycles are verified. Qwen3-ASR 0.6B Q8_0 is now pinned as a separate model component, but real ASR and CUDA inference remain unverified. The later engine families below remain research/architecture plans.

This document records the recommended engine families, why they were selected, known limitations, and the order in which they should be integrated. Upstream capabilities and model compatibility are not the same as Local AI Runtime support. A component must not be advertised as supported until its exact artifact, model, target, and real task have passed the runtime's acceptance checks.

## Executive decisions

1. **Freeze non-critical LLM/Chat polish.** Keep `llama.cpp` as the current Chat engine. Do not spend another milestone on cosmetic Markdown or prompt-quality refinements unless there is a security, data-loss, or basic-chat correctness defect.
2. **Start Stage 13 with `audio.cpp`.** It offers one native C++/ggml runtime for ASR, TTS, VAD, source separation, voice conversion, and music tasks. Its official Windows CUDA package prerequisites match the user's RTX 3060 Laptop GPU and driver. Start with Turkish ASR, then Turkish TTS; do not begin with music generation.
3. **Keep `whisper.cpp` and `sherpa-onnx` as alternatives, not simultaneous first integrations.** Compare them only after the audio.cpp ASR path works on the target machine.
4. **Use `stable-diffusion.cpp` for the first image-generation pilot.** Begin with a conservative SD 1.5, 512x512 task and one inference process at a time.
5. **Use ONNX Runtime plus OpenCV for vision/OCR.** OpenCV is image I/O and preprocessing, not by itself an OCR/vision model. Choose a real ONNX model separately. On Windows 11 24H2 or later, evaluate WinML first; avoid making new work depend on DirectML, which is documented as being in sustained engineering.
6. **Treat FFmpeg as media infrastructure, not an AI model/engine.** Add it when audio/video workflows require decode, resample, remux, or frame extraction; audit build configuration and redistribution obligations.
7. **Defer COLMAP/Open3D, music generation, MCP/Agent orchestration, and broad UI redesign until the first audio tasks work.**

## Hardware profile and operational constraints

| Item | Observed target value | Engineering consequence |
|---|---:|---|
| OS / architecture | Windows x64 (exact Windows build not yet recorded) | First pilot must use Windows x64 artifacts; capture `winver` before choosing WinML. |
| GPU | NVIDIA GeForce RTX 3060 Laptop GPU | Use CUDA only when the engine's stated runtime requirements are met. |
| Dedicated VRAM | 6,144 MiB | Do not load the LLM, ASR, TTS, and image model concurrently. |
| Compute capability | 8.6 | Meets audio.cpp's documented CUDA compute-capability floor of 7.5. |
| NVIDIA driver | 616.92 | Meets audio.cpp's documented Windows CUDA driver floor of 580. |
| System RAM | 16 GB | Prefer one managed inference server at a time; test CPU fallback separately. |
| Logical CPUs | 16 | Use upstream `balance` Windows package profile unless measurement proves otherwise. |

The previous Qwen3 4B CUDA session produced about 85–91 generated tokens/s in engine timing logs and external NVIDIA process/utilization evidence. However, the runtime status still reported `gpu_execution_evidence: not-verified`, with no app-side offloaded-layer count. Keep this distinction in diagnostics and in future engine acceptance reports. Do not treat whole-system GPU utilization as proof that a specific managed process used the GPU.

The audio.cpp server documentation warns that lazy-loaded models remain resident until the server exits; they are not automatically unloaded at the end of a request. The first integration should therefore serialize GPU-heavy model use and stop/restart the audio process when switching model families if necessary.

## 1. Audio stack — Stage 13 (first implementation target)

### 1.1 Primary project: audio.cpp

- **Upstream:** <https://github.com/0xShug0/audio.cpp>
- **Release reviewed:** `v0.9.3`, published 2026-10-26.
- **Project license:** Apache-2.0. This does not replace the separate license of each model checkpoint.
- **Windows package guidance:** <https://github.com/0xShug0/audio.cpp/blob/main/docs/build/windows.md>
- **Model license register:** <https://github.com/0xShug0/audio.cpp/blob/main/docs/model_licenses.md>
- **Release assets:** <https://github.com/0xShug0/audio.cpp/releases/tag/v0.9.3>

The upstream project describes itself as a native C++ inference framework based on ggml and covers ASR, TTS, VAD, speaker diarization, voice conversion, source separation, music generation, and related audio tasks. The supported-model table is model-family-specific: supported task, language, precision, backend and streaming behavior differ by model. A model's presence in that table does not prove that every backend/quantization combination works.

### 1.2 Windows package choice

The official `v0.9.3` release asset list was re-opened on 2026-10-31. The exact Windows x64 assets selected for the Local AI Runtime allowlist are:

| Role | Exact release asset | Exact bytes (GitHub release API) | SHA-256 |
|---|---|---:|---|
| CPU server | `audio-v0.9.3-bin-windows-x64-cpu.zip` | 30,547,092 | `3ac66f46395b4e92e4f3bb68b200f0032310bd9922f12d97f10ccdadb02dee4b` |
| CUDA 13.3 server | `audio-v0.9.3-bin-windows-x64-cuda13.3.zip` | 277,336,614 | `0bd3988be10c31ec078c40df2bd0d4266e660804f6d5083aeb05d1309d091e43` |
| CUDA 13.3 runtime | `audio-v0.9.3-cudart-windows-x64-cuda13.3.zip` | 575,457,454 | `839eb20f684a8aab64895b1e466b32ce9b29dfc19dc1ce15b2ef35218512ac0a` |

These differ from the older generic `audiocpp-windows-*-balance.zip` names in the original research notes. The standard CPU server archive is the selected general-purpose CPU pilot; v0.9.3 also publishes a separate `audio-v0.9.3-bin-windows-x64-cpu-portable.zip` (SHA-256 `f5fa4662b7ec323dbe31d37e224e46159b064836a9005da384551a5248771eeb`, release page label 29 MB) for broader baseline-ISA compatibility. The CUDA package is specifically a CUDA 13.3 server archive plus its separate CUDART archive. The user queried the official GitHub release API at `https://api.github.com/repos/0xShug0/audio.cpp/releases/tags/v0.9.3` from Windows PowerShell 5.1 on 2026-10-31 and supplied the exact `assets[].size` values recorded above. The compiled allowlist and regression tests now require an exact match for filename, canonical URL, SHA-256 and byte size. These API-reported lengths still require the Windows installer to download and independently verify the actual bytes before activation.

The published Windows CUDA prerequisites remain compute capability 7.5 or newer and NVIDIA driver 580 or newer. Model files are separate. The prebuilt archive supplies runtime DLLs; the user should not need to install the CUDA Toolkit or Visual Studio Build Tools merely to execute the prebuilt package. The CUDA build also contains the CPU backend, allowing CPU fallback selection after package installation.
The user's GPU (compute capability 8.6) and driver (616.92) meet these published prerequisites. This is **package eligibility only**, not a claim that a given audio model fits in 6 GB VRAM or that CUDA inference has been validated.

**Pinning rule:** do not resolve a floating `latest` release at installation time. For the catalog, pin `v0.9.3`, exact approved asset names, exact byte sizes and SHA-256 digests obtained from the official release API. Confirm that the selected pair is from the same release. Re-check release state immediately before implementation; this research snapshot must not be treated as a permanent version pin.

### 1.3 First model tasks

**ASR first: Qwen3-ASR-0.6B**

- The upstream audio.cpp model table lists `qwen3_asr` and explicitly includes Turkish (`tr`) among the supported languages.
- The audio.cpp model docs describe a GGUF package path and show a native model-manager example for `qwen3_asr_0_6b_q8_0`.
- The original Qwen model card identifies the checkpoint as Apache-2.0 and lists multilingual recognition including Turkish.
- For known-language Turkish recordings, pass an explicit `tr` language hint. The audio.cpp model docs warn that quantization can reduce automatic language-detection reliability even when transcription remains good with an explicit language hint.
- Preferred initial test: a short, non-sensitive, licensed Turkish WAV fixture with a human-checked reference transcript. Record WER/CER or at minimum a reviewed transcript comparison, elapsed time, real-time factor, RSS, peak VRAM, and backend evidence.

**TTS second: Chatterbox 0.5B multilingual**

- The audio.cpp model table lists the Chatterbox 0.5B family and includes Turkish (`tr`) in its language list.
- The original `ResembleAI/chatterbox` model page states MIT licensing and lists Turkish among the supported multilingual languages.
- The audio.cpp table indicates FP16/Q8 paths for this model family; do not assume a Q4 model is available.
- Treat 6 GB VRAM fit as unverified. Run ASR first, capture peak memory, stop the ASR process, then test TTS in isolation. Evaluate whether the Turkish speech is understandable and natural; a successful WAV file alone is not quality acceptance.

**Music later: ACE-Step 1.5**

- audio.cpp lists ACE-Step 1.5 Turbo/Base under music/edit tasks.
- ACE-Step's own VRAM guidance places GPUs at or below 6 GB in a constrained tier: it recommends a 2B Turbo DiT without the language model by default, with INT8 and CPU offload in the reference implementation. This guidance is not a measured guarantee for the audio.cpp port.
- Do not include music generation in the Stage 13 acceptance gate. Revisit as a later advanced-audio experiment after ASR and TTS are stable.

### 1.4 Audio.cpp runtime protocol and adapter implications

The upstream server documentation lists these endpoints:

- `GET /health`
- `GET /v1/models`
- `POST /v1/audio/speech`
- `POST /v1/audio/transcriptions`
- `POST /v1/tasks/run`

This is a task-oriented audio API, not the current Local AI Runtime OpenAI-compatible Chat interface. The existing C++ `EngineAdapter` contract v1 exposes readiness paths plus `chat_completions_path`; a generic `openai-compatible-process` adapter must not be used to pretend audio.cpp is a chat engine.

**Required Local AI Runtime design before catalog activation:**

1. Keep the current Chat `EngineAdapter` behavior and `llama.cpp` stable.
2. Add an explicit task-engine / task-adapter contract (or a clearly versioned adapter v2) that declares supported task capabilities, typed request routes, output artifacts, readiness, process lifecycle and cancellation behavior.
3. Add normalized local runtime operations for at least `asr.transcribe` and `tts.synthesize`. Keep binary upload/output paths bounded and under the user data directory; never accept arbitrary filesystem paths or commands from a model/request.
4. Reuse existing process management, FIFO job queue, cancellation, Activity history and local resource telemetry where the semantics fit. Audio/model content should not enter diagnostics by default.
5. Represent ASR/TTS output as typed task results (transcript text or generated audio file metadata), not a fake `chat.completion` response.
6. Since audio.cpp lazy loading keeps models resident until process exit, expose and test an explicit engine stop/restart / model-switch policy before enabling multiple models simultaneously.

### 1.5 Catalog and installer work required

The manifest-v1 installer allowlist supports the constrained `llama-release`, `pinned-gguf` and narrowly allowlisted `audio-cpp-release-set` strategies. The public component-v1 schema describes engines/models but does not itself make an installer available. The `pinned-gguf` strategy supports one file per model, with safe nested Hugging Face repository subdirectories, immutable revision, exact file name, byte size and SHA-256.

Stage 13A implemented the fail-closed `audio-cpp-release-set` manifest strategy for the pinned v0.9.3 Windows x64 CPU archive and CUDA 13.3 server + CUDART pair. It validates exact upstream filenames, URLs, SHA-256 and exact release API byte sizes; downloads into per-user staging; safely extracts and merges regular files/directories only; then atomically activates and registers the non-Chat task engine with rollback. On the target Windows machine, both CPU and CUDA package sets were installed by the managed installer in separate isolated data roots. Both managed processes started, passed readiness and `/health` (`backend=cpu` and `backend=cuda` respectively), and stopped with their process and listening port gone. The CPU and CUDA runtime package lifecycle is therefore accepted. The CUDA health test had `models=0`; `nvidia-smi` showed no compute process, so no CUDA inference claim is made. The Local AI Runtime generated config sets `ui_management: true` because audio.cpp v0.9.3 rejects an empty model list otherwise. This strategy installs engine packages only; model weights are separate components.

Stage 13B now pins Qwen3-ASR 0.6B Q8_0 as a separate single-file GGUF model: audio-cpp-gguf revision `4c9a3098d552a0f3d9b2adc11638a11022c07d7e`, file `qwen3-asr-0.6b-q8_0.gguf`, exact size `1151272416`, SHA-256 `6c44ec2fb4cee513892d7863c1fcc3ea6b699ffa4d899b0ef4ab19956d9544f7`, Apache-2.0. The install workflow verifies exact size, SHA-256 and GGUF magic, then adds `id`, `family=qwen3_asr`, `path`, `task=asr`, `mode=offline` to the managed audio.cpp server config. If the model is installed first, the engine installer registers it when configuring the new server; if the engine is already installed, the model installer updates the config only while the engine is stopped. Conflicting registrations fail closed. The real Turkish ASR task is still unverified and should be run with explicit `language=tr`.

Do not execute an upstream Python model-manager script as an installer. The native `audiocpp_model_manager` exists in certain native builds, but Local AI Runtime owns a reviewed, declarative, digest-verified download plan rather than running repository-provided commands from a model or engine package.
## 2. Speech alternatives (defer until audio.cpp comparison)

### whisper.cpp

- **Upstream:** <https://github.com/ggml-org/whisper.cpp>
- **Latest release visible at research time:** `v1.9.5`, published 2026-10-06.
- **Project license:** MIT.
- **Role:** independent ASR baseline/fallback for comparing Turkish transcription quality, latency and resource usage.
- **Why not first:** audio.cpp already includes a Turkish-listed Qwen3 ASR route plus TTS in the same audio server. Integrating Whisper first would add a second engine family before the unified audio path has been evaluated.
- **Acceptance if selected:** pin exact release assets or build recipe, test Windows x64 CPU first, then CUDA; use the same WAV/reference transcript as audio.cpp.

### sherpa-onnx

- **Upstream:** <https://github.com/k2-fsa/sherpa-onnx>
- **Latest release visible at research time:** `v2.0.1`, published 2026-10-26.
- **Project license:** Apache-2.0 (confirm exact release and any third-party notices at integration time).
- **Windows x64:** upstream documentation publishes Windows x64 native shared/static libraries and executables.
- **Role:** alternative offline speech SDK for ASR, TTS, diarization, VAD, enhancement and source separation.
- **Caveat:** language and quality are model-specific. Do not advertise Turkish TTS just because the SDK has TTS support; select an exact Turkish-capable checkpoint, inspect its license, pin it and run a real task.

## 3. Image generation — Stage 14

### stable-diffusion.cpp

- **Upstream:** <https://github.com/leejet/stable-diffusion.cpp>
- **Project license:** MIT.
- **State:** active development; the upstream README warns that APIs and CLI options may change frequently.
- **Capabilities:** C/C++ diffusion inference including SD 1.x/2.x, SDXL and several newer image/video families; CUDA build is documented, as is CPU-only fallback.
- **GPU note:** upstream CUDA build documentation recommends at least 4 GB VRAM. The user's 6 GB GPU is a candidate, not a validated profile. Source CUDA build requires CUDA Toolkit; do not assume an official, turnkey Windows binary exists for every release.
- **First task:** start with SD 1.5 at 512x512, low batch size, one image-generation process, and LLM/audio engines stopped. Capture exact model revision, file list, license, SHA-256, generation time, peak VRAM and output image validity.
- **Do not start with:** SDXL, FLUX, video models or large high-resolution generation on the 6 GB laptop GPU.

## 4. Vision and OCR — Stage 15

### ONNX Runtime + OpenCV

- **ONNX Runtime:** <https://onnxruntime.ai/>; execution provider list: <https://onnxruntime.ai/docs/execution-providers/>.
- **Windows guidance:** <https://onnxruntime.ai/docs/get-started/with-windows.html>.
- ONNX Runtime is the model execution layer; OpenCV (<https://opencv.org/>) handles image/video I/O and preprocessing. OCR/object-detection still require a separately selected model and task-specific post-processing.
- ONNX Runtime lists CPU, CUDA, TensorRT, DirectML and other execution providers. Its Windows docs recommend WinML on Windows 11 24H2+ for broader hardware/provider selection; DirectML is documented as sustained engineering. The exact Windows version on the user's machine is not yet recorded.
- **First task:** detect Windows version; add CPU baseline; if Windows 11 24H2+ evaluate WinML; otherwise evaluate a maintained execution provider that matches the installed GPU/driver. Do not adopt DirectML as the long-term default without a specific compatibility reason.
- **First model task:** one pinned ONNX OCR model or one small object detector. Do not add several vision models at once. Record source, model license, tokenizer/config assets, exact files and hashes.

## 5. Media and video foundation — Stage 16

### FFmpeg

- **Upstream:** <https://ffmpeg.org/>.
- **Release visible at research time:** `9.0.2`, published 2026-09-18; also an 8.1 maintenance branch exists.
- The FFmpeg project distributes source and links to Windows executable distributors such as Gyan and BtbN. Official release tarballs have PGP signatures; verify authenticity when using upstream source releases.
- FFmpeg is not an AI engine. It provides format/container/codec conversion, audio extraction/resampling and video frame extraction for AI task pipelines.
- **Licensing:** FFmpeg licensing depends on build configuration and enabled libraries. Select and audit an LGPL-compatible build if that is the intended product policy; do not accidentally bundle GPL-enabled components. Preserve notices and required source/relinking obligations. Use the exact builder's configuration and provenance, not just its executable filename.
- **First task:** use FFmpeg only as a subprocess/library for a concrete ASR or vision workflow (e.g. MP4 → 16 kHz WAV or video → sampled frames). Restrict arguments/options to a fixed allowlist; never expose arbitrary shell commands to the UI.

## 6. 3D reconstruction — Stage 18

### COLMAP + Open3D

- **COLMAP:** <https://github.com/colmap/colmap> / <https://colmap.github.io/>.
- **Open3D:** <https://www.open3d.org/>.
- COLMAP performs structure-from-motion and multi-view stereo reconstruction; Open3D supplies point-cloud/mesh processing and visualization. They are complementary, not interchangeable AI engines.
- Open3D v0.20 was announced on 2026-09-24 and has Windows/C++ support; use the official C++ library packages and verify its exact license/notices at integration time.
- Defer this stack until the audio, image and vision task adapters are stable. Start with a tiny, licensed photo set; 16 GB RAM is a practical constraint for larger reconstructions.

## 7. Advanced audio/music — Stage 19

ACE-Step 1.5 is listed by audio.cpp for music/edit tasks and supports a broad set of languages. The official ACE-Step GPU guidance classifies <=6 GB GPUs as a constrained tier, recommending a 2B Turbo DiT without its language model by default, using INT8 and CPU offload in its reference stack. This is not proof that the audio.cpp implementation will fit or be fast on the RTX 3060 Laptop GPU. Treat it as a later experiment after ASR/TTS and monitor memory closely.

## Proposed delivery order

| Stage | Work | Selected implementation | First acceptance task |
|---|---|---|---|
| 12 / 12.5 residuals | Keep existing LLM stable; only critical fixes | llama.cpp + current UI | Do not block audio work on cosmetic polish; preserve security and basic-chat quality gates. |
| 13A | Task-engine adapter and audio runtime package | audio.cpp v0.9.3 pilot | Windows x64 CPU and CUDA 13.3 managed install, readiness/health and clean stop are verified; inference is separate. |
| 13B | Turkish speech recognition | Qwen3-ASR-0.6B Q8_0 via audio.cpp | Install exact-pinned model; same fixed WAV and reference transcript on CPU, then CUDA. |
| 13C | Turkish text-to-speech | Chatterbox 0.5B via audio.cpp | Generate WAV from fixed Turkish text, listen, inspect duration/sample rate and memory. |
| 13D | ASR comparison | whisper.cpp, optionally sherpa-onnx | Same WAV, compare CER/WER, latency, quality and memory; only add a second runtime if it wins materially. |
| 14 | Image generation | stable-diffusion.cpp | SD 1.5, 512x512, single image. |
| 15 | Vision/OCR | ONNX Runtime + OpenCV | One pinned OCR or detection model on a small image. |
| 16 | Audio/video processing | FFmpeg plus Stage 13/15 task APIs | MP4 audio extraction and deterministic video frame sampling. |
| 17 | Documents and local knowledge | Existing/selected embedding + RAG stack | Small local document → indexed chunks → grounded retrieval. |
| 18 | 3D reconstruction | COLMAP + Open3D | Small photo set → point cloud/mesh. |
| 19 | Advanced audio/music | audio.cpp ACE-Step or a better verified candidate | Very short generation with constrained memory; not a v1 promise. |
| 20 | MCP / Agent orchestration | Local task APIs + MCP stdio | Expose only already-tested tasks as safe local tools. |
| 21 | Distribution trust / v1.0 | signing, provenance, platform parity | Clean-machine install/update/recovery, signing and release evidence. |

## Universal integration and release gates

Every new engine family must pass these gates before it is marked installed/supported in the product:

1. **Upstream review:** maintained official source, immutable release/commit, exact platform assets and build/runtime prerequisites.
2. **License review:** engine license, every model-weight license, third-party runtime notices and redistribution terms are separate checks.
3. **Artifact integrity:** HTTPS source, exact byte size and SHA-256 per archive/model file; signed upstream provenance when available. SHA-256 alone is integrity, not publisher identity.
4. **Safe install:** declarative allowlisted installer, bounded downloads, safe archive extraction, per-user data directory, staging/atomic activation and rollback. No upstream install scripts or model-provided commands.
5. **Lifecycle:** start/stop, health, readiness, process identity, cancellation, restart recovery, queue behavior and explicit model-switch memory policy.
6. **Real task:** at least one deterministic CPU test and one target-GPU test where that backend is claimed. Record actual task output, latency/RTF, RAM/RSS, VRAM before/peak/after and engine evidence.
7. **Honest evidence:** distinguish `listed`, `package-verified`, `installed`, `process-ready`, `task-tested`, and `profile-validated`. Do not mark a task/target supported from hosted fake-engine CI.
8. **User experience:** one obvious task entry point; do not make users navigate between catalog, Activity and Settings to finish one workflow. Activity retains detailed logs and diagnostics.

## Research source register (checked 2026-10-31)

- [audio.cpp README and supported-model matrix](https://github.com/0xShug0/audio.cpp) — native C++ task coverage and model-family language/format support.
- [audio.cpp v0.9.3 release](https://github.com/0xShug0/audio.cpp/releases/tag/v0.9.3) — reviewed release tag.
- [audio.cpp Windows build and prebuilt packages](https://github.com/0xShug0/audio.cpp/blob/main/docs/build/windows.md) — CPU/CUDA package names, balance profile and runtime requirements.
- [audio.cpp server API and model configuration](https://github.com/0xShug0/audio.cpp#server) — health/models/audio endpoints and lazy-load behavior.
- [audio.cpp model license register](https://github.com/0xShug0/audio.cpp/blob/main/docs/model_licenses.md) — per-family model licenses; verify exact model version again before catalog approval.
- [Qwen3-ASR-0.6B model card](https://huggingface.co/Qwen/Qwen3-ASR-0.6B) — Apache-2.0 and multilingual ASR, including Turkish.
- [Chatterbox model card](https://huggingface.co/ResembleAI/chatterbox) — MIT model license and Turkish language support.
- [audio.cpp Qwen3 model conversion/runtime guide](https://github.com/0xShug0/audio.cpp/blob/main/docs/models/qwen3.md) — GGUF sidecars, Qwen3-ASR invocation and explicit language hint.
- [whisper.cpp releases](https://github.com/ggml-org/whisper.cpp/releases) and [MIT license](https://github.com/ggml-org/whisper.cpp/blob/master/LICENSE) — secondary ASR comparison candidate.
- [sherpa-onnx v2.0.1 release](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v2.0.1) and [Windows x64 prebuilt documentation](https://k2-fsa.github.io/sherpa/onnx/install/windows/generated/download/windows_x64.html) — speech SDK alternative.
- [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) and [CUDA build guide](https://github.com/leejet/stable-diffusion.cpp/blob/master/docs/build.md) — image-generation candidate and build prerequisites.
- [ONNX Runtime Windows guidance](https://onnxruntime.ai/docs/get-started/with-windows.html) and [execution providers](https://onnxruntime.ai/docs/execution-providers/) — WinML/CPU/CUDA/DirectML provider decisions.
- [OpenCV](https://opencv.org/) — image and video I/O/preprocessing.
- [FFmpeg 9.0.2 release/download information](https://www.ffmpeg.org/download.html) — version, Windows binary distributors and signed source release guidance.
- [COLMAP](https://github.com/colmap/colmap) and [Open3D v0.20](https://www.open3d.org/) — deferred 3D stack.
- [ACE-Step 1.5 GPU compatibility guidance](https://github.com/ace-step/ACE-Step-1.5) — constrained <=6 GB reference configuration; not a guarantee for audio.cpp's port.

**Revalidation rule:** upstream releases, supported model matrices, CUDA requirements, licenses and package assets change. Re-check the exact upstream release/commit and model card on the day an installer or model pin is implemented. This research document is a dated decision record, not a permanent compatibility guarantee.
