# Product information architecture

This document defines the user-facing organization of Local AI Runtime. The dashboard must remain understandable as the number of engines and model families grows; implementation details and download artifacts are not the primary navigation model.

## Application navigation

The current dashboard is organized into five persistent destinations:

1. **Overview** — local runtime readiness, current chat model, hardware summary, and clearly-labelled available/planned workspaces.
2. **Chat** — the primary user task, with a dedicated composer, incremental SSE token streaming, stop-generation behavior, and a bounded local conversation history. Conversations are stored in browser/WebView local storage under the per-user profile and are never uploaded or included in diagnostics.
3. **Models** — reviewed engine packages and model weights, their dependencies, requirements and licenses, plus the active Chat Runtime Profile. The component catalogue is sourced from the reviewed build-time embedded manifest. Installing an engine package or downloading a model never silently activates it; the user explicitly applies an installed engine/model combination here.
4. **Activity** — installed engine state plus persistent download, setup, engine start/stop, and inference operation records. `GET /v1/activity` follows [activity-v1](../catalog/schemas/activity-v1.schema.json); `GET /v1/jobs` returns the shared [jobs-v1](../catalog/schemas/jobs-v1.schema.json) lifecycle contract. The Activity endpoint is a point-in-time snapshot of current engine readiness and durable job state, not an append-only event timeline. Active streamed inference can be canceled from Activity; only reviewed catalog/update jobs support retry.
5. **Settings** — language preference, release/update controls, advanced manual/custom engine configuration, and privacy information. Manual llama.cpp setup is an escape hatch for user-managed binaries/models; managed catalogue profiles are selected in Models.

The UI ships English and Turkish resource files under `web/locales/`. The loopback server serves only `/locales/en.json` and `/locales/tr.json`; arbitrary locale paths are not exposed. The published localization slice covers navigation, onboarding, Activity states, engine/model guidance, update progress and primary error framing. Source-provided upstream diagnostics and newly introduced task-specific messages must continue to be reviewed for translation coverage as each workspace is added.

## Separate tasks from components

- **Workspace** answers “What do I want to do?” Examples: chat, image generation, image understanding, transcription, speech, music/SFX, video, local knowledge search, and automation.
- **Engine** is an executable runtime or adapter that performs one or more tasks (for example `llama.cpp`).
- **Backend** is the execution implementation/package selected for an engine (for example CPU or a CUDA 12/13 build). Backend packages may coexist side-by-side; installing another backend must not change the active selection.
- **Model** is a versioned weight/checkpoint used by an engine; quantizations and multimodal projector files are distinct catalogue artifacts with their own resource and license metadata.
- **Runtime Profile** is the explicit pairing of an engine package/backend, a model, and runtime settings such as context size. It is persisted separately from installation state and activated only by an explicit user action while engines are stopped.
- **Dependency** is a package required by an engine (for example the official `cudart` DLL archive required beside a CUDA-enabled `llama-server`). Dependencies are installed and verified as part of the engine choice, not exposed as duplicate engines to users.
- **Installed artifact** means a verified engine package or model file is present on disk. It does not mean that artifact is active.
- **Active instance** is the selected Runtime Profile currently configured for Chat, with process, readiness, and last-task status reported separately.
- **Job** describes a component download/verification/install/update or a runtime operation such as engine start/stop and inference. The current API exposes durable lifecycle records and a point-in-time Activity snapshot; the target model adds a separate append-only event trail. Retry is allowlist-restricted and cancellation is available only where the operation can stop safely.
- **Compatibility profile** records observed hardware/software and the evidence for a specific engine/model/task combination. Hardware detection alone is not a support claim.

## User-facing workspaces

| Workspace | User tasks | Current status | Candidate open-source runtime families (not yet supported claims) |
|---|---|---|---|
| Language & chat | Chat, text generation, structured prompting | Preview: `llama.cpp` + pinned GGUF starter model | `llama.cpp`; later evaluate other local C/C++ runtimes against the adapter contract |
| Image generation | Text-to-image, image-to-image, inpainting | Planned | Evaluate `stable-diffusion.cpp` and compatible native runtimes |
| Vision & OCR | Image question answering, OCR, document/image understanding | Planned | Vision-capable `llama.cpp` models, OpenCV and dedicated OCR runtimes where licensing/platform support is verified |
| Speech & audio | Speech recognition, transcription, text-to-speech, audio understanding | Planned | Evaluate `whisper.cpp`, `sherpa-onnx`, and other maintained native runtimes per task |
| Music & SFX | Music generation, sound effects, audio continuation | Planned | Separate model/runtime entries only after license, binary, memory, and real-task validation |
| Video | Video understanding, frame analysis, generation | Planned | Start with frame extraction + vision workflows; generation is a separate, higher-resource capability |
| Knowledge & search | Embeddings, reranking, local document retrieval | Planned | Native embedding/reranking adapters; index storage is a separate local component |
| Automation & tools | Reusable local workflows and integrations | Planned | A constrained workflow layer after process isolation, permissions, and audit logging are designed |

The dashboard may show future workspaces to make the product direction clear, but only working workspaces are actionable. Planned cards must say `Planned` and must not look like broken buttons.

## Runtime Profile and model activation rules

The installation lifecycle and activation lifecycle are separate:

1. **Install engine package** — verifies and stores a selected CPU/CUDA package. Multiple reviewed backend packages may coexist. Installing an additional package never replaces the active executable.
2. **Install model** — downloads, verifies and stores the selected model artifact. It never changes the model currently used by Chat.
3. **Apply Runtime Profile** — the user selects one installed engine package/backend, one installed Chat model, and a context size. The runtime verifies the model size, digest and format, then persists the profile. Applying is rejected while a managed engine is running.
4. **Start** — Activity starts only the executable/model pair in the active profile. A Chat engine without a model cannot be started.
5. **Ready** — the process must pass its local readiness check before Chat Send is enabled. Typing a prompt does not override the readiness gate. API callers receive `503` while the model is missing or the engine is not ready; these attempts are recorded in Activity when an engine can be identified.

`GET /v1/runtime/profile` and `POST /v1/runtime/profile` follow `runtime-profile-v1`. `engine_component_id` and `model_component_id` may be empty for Advanced/manual configuration; this is distinct from a managed catalogue profile. `can_chat` means an executable and model path are configured, not that the process is currently ready. Live readiness comes from `/v1/engines`.

For managed llama.cpp profiles, CPU requests `--n-gpu-layers 0`; CUDA packages request `--n-gpu-layers auto`. A CUDA package installation or GPU detection alone is not proof of actual GPU inference. Driver/runtime compatibility and offloaded-layer/task evidence remain separate acceptance gates. The current Runtime Profile UI exposes only settings supported by the current llama.cpp adapter; advanced tuning and automatic hardware selection are future work.

## Catalog navigation

The component library is a resource browser, not the primary list of user tasks. The Models view includes the Active Chat Runtime Profile above the catalogue so users can distinguish what is installed from what Chat will actually start. Catalog v2 presents model weights and engine runtime packages in separate sections; a backend package is never presented as another model. Installed components show a compact status and a profile-selection action instead of a large disabled “Installed” button. “Select for Chat” and “Select engine” only draft the profile; the user must still explicitly apply it. Its main filters are:

1. **Engines** — runtime family, task capabilities, supported backend, platform, version, and required dependencies.
2. **Models** — task/modality, family, format, quantization/precision, language, model size, license, disk size, and estimated system RAM/VRAM.
3. **Supporting packages** — shown only where the user needs to understand a prerequisite; low-level CUDA DLL archives and similar assets remain dependencies of a selected engine.
4. **Installed** — installed version, source, verification status, assigned model, running/ready state, and actions.
5. **Activity & diagnostics** — persistent operation rows, engine state and the FIFO component-install queue. Queued jobs show their position and reviewed dependency plan; active transfer cancellation and explicit retry remain separate actions. A future append-only event ledger will add a richer ordered timeline for validation, engine start/stop, inference failures, update and rollback events.

Search should match task, model family, architecture, parameter count, format, quantization, backend, hardware requirement, and license. Model-task filters are generated from reviewed metadata groups (`chat`, `vision`, `embeddings`, `speech`, `image-generation`, `video`, `audio`) and only appear when the catalogue contains more than one task group. License acknowledgement is preserved across background catalogue refreshes. Incompatible items should be hidden from the default recommendation view or disabled with a concise reason, while advanced users may inspect the full catalog.

## Versioned component metadata contract

Every catalog API entry must follow the published [component-v1 schema](../catalog/schemas/component-v1.schema.json), which is validated against the actual `/v1/catalog` response in HTTP smoke tests. The current contract includes the following fields; Catalog v2 currently describes the task-first presentation and additive optional `model_profile` metadata; `/v1/catalog` remains backward-compatible and its v1 semantics are not changed. Future incompatible API changes require a separate versioned endpoint. Model profile fields include task group, architecture, parameter count, format, and quantization:

- Identity: `schema_version`, stable `id`, `type` (`engine`/`model`), `name`, `family`, `version`/version policy.
- Purpose: `workspaces`, `tasks`, `modalities`, `input_formats`, `output_formats`, and capabilities.
- Compatibility: OS, architecture, CPU feature requirements, backend (`cpu`, `cuda`, `vulkan`, `metal`, etc.), driver/runtime requirements and known exclusions.
- Resources: download size, installed size, minimum/recommended system RAM, minimum/recommended VRAM, disk-space overhead, expected context/working-memory impact where known.
- Provenance: canonical HTTPS source, immutable release/revision, published digest/signature, license identifier and exact license URL, plus dependency IDs.
- Evidence: package hash verified, install/probe status, process-ready status, real-task test status, test date, software versions, and hardware profile identifier.
- Lifecycle: install directory policy, configuration adapter, readiness probe, start/stop semantics, log locations, uninstall behavior, update/rollback policy.

No arbitrary catalog URL may be accepted by the runtime API. Model repositories must not be allowed to contribute scripts that the application executes. Packages are resolved against official metadata/pinned revisions, checksummed, safely extracted, and registered only after the complete dependency set verifies.

## Local hardware inventory

The first hardware endpoint is `GET /v1/system/hardware`. It returns OS and architecture, logical CPU count, total/available system RAM, available bytes on the user-data volume, a GPU probe state, and any graphics adapters exposed by the platform probe. Windows uses DXGI; Linux reads the local DRM/sysfs inventory; GPU enumeration on macOS is not implemented in this first slice. The endpoint is loopback-only and does not upload hardware data.

The live resource endpoint is `GET /v1/system/resources` (`resources-v1`). It returns system CPU utilization and memory use/pressure, plus CPU and resident memory for each managed engine process. CPU percentages are computed from consecutive samples; the first reading is `null`. NVIDIA utilization and VRAM are included only when `nvidia-smi` is available and are explicitly system-wide; AMD/Intel/Apple live GPU utilization is currently unavailable rather than estimated. Snapshots are transient, local-only, and not stored in diagnostics. Memory pressure is a heuristic; the runtime does not automatically stop an engine or change its backend based on these readings.

The dashboard exposes an initial heuristic recommendation for the pinned Qwen2.5 1.5B Q4_K_M CPU starter based on total/available RAM and free disk, and an NVIDIA CUDA candidate only when a local adapter is detected. These recommendations are screening hints, not compatibility claims. GPU presence and dedicated-memory reports are observations, not inference evidence. Engine status reports the installed package family, requested backend, and session-scoped startup-log evidence: `cuda-runtime-reported`, `offload-reported`, `no-layers-offloaded`, or `not-verified`. A positive offloaded-layer message is evidence that the engine reported layer placement during startup; it does not measure live GPU utilization during inference. The opt-in “Measure GPU during a local test” action sends a fixed local chat request and, when `nvidia-smi` is available, samples system-wide utilization/memory during that request. It does not collect or save the generated response, and measurements may include other applications; this must not be described as process-specific proof. Do not automatically mark an engine/model combination compatible from inventory, startup log, or system-wide sample alone.

## Evidence states

Show these states separately; never collapse them into one “supported” badge:

1. `Listed` — reviewed metadata exists.
2. `Package verified` — selected official artifact(s) match size/hash/provenance policy.
3. `Installed` — files were extracted and registered.
4. `Process ready` — executable starts and its health/model API answers.
5. `Task tested` — a real task completed successfully for a recorded model/backend.
6. `Profile validated` — repeated task, memory, startup and shutdown checks passed on a named OS/driver/hardware profile.

A CUDA package being downloadable or a GPU being present does not mean inference used the GPU. Report selected backend, GPU detection evidence, driver/runtime check, offloaded layers when available, VRAM observation where available, and CPU fallback explicitly. Keep startup log evidence distinct from measured activity during an actual inference task.

## Diagnostics and support

Every job emits structured local events with `job_id`, `component_id`, stage/phase, timestamp, progress bytes, attempt count, error code, sanitized actionable message, and recovery hint. Preserve the selected upstream asset names and hashes in the diagnostic report. Never log prompts or generated text in runtime-owned events. Engine logs remain opt-in because upstream runtimes may log prompt fragments or local paths. Diagnostics are local-only and user-exported; nothing is uploaded automatically.

## Implementation order

1. [x] Establish the five-area UI shell, task-first Overview, dedicated Chat, and initial EN/TR resources.
2. [x] Unify operation state in `activity-v1` and durable `jobs-v1`; component retry remains allowlist-restricted and streamed inference supports explicit cancellation.
3. [x] Complete the first localization/accessibility/onboarding slice; native screen-reader review and broader clean-OS acceptance remain open.
4. [x] Move reviewed model, engine and dependency definitions to the embedded versioned authoring manifest.
5. [x] Separate install from activation with explicit Runtime Profiles, persistent model/backend selection and Chat readiness gates (`v0.1.19-alpha`).
6. [x] Add a persistent single-worker FIFO installation queue, idempotent duplicate handling, bounded queue capacity, queued cancellation, safe pending-job recovery and reviewed CUDA runtime dependency plans (`v0.1.20-alpha`).
7. [~] Catalog v2 presentation: separate Models/Engine runtimes, optional model profile metadata, task filters, compact installed/selected actions; additional reviewed model families remain future work.
8. [ ] Finalize and test the shared engine adapter lifecycle against the existing `llama.cpp` integration.
9. [ ] Validate reproducible CPU/GPU hardware profiles and add Qwen3.5/Gemma 4 as model-family pilots on the existing engine where formats and capabilities are verified.
10. [ ] Deliver a constrained stdio MCP adapter, then Documents/RAG and the first Vision/Speech pilots.
11. [ ] Add image generation, video and music only after their distinct adapters, licences, hardware requirements, and real-task tests are ready.
12. [ ] Complete signing, provenance, Linux/macOS install/update/recovery parity and the v1.0 acceptance gates.
