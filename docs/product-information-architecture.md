# Product information architecture

This document defines the user-facing organization of Local AI Runtime. The dashboard must remain understandable as the number of engines and model families grows; implementation details and download artifacts are not the primary navigation model.

## Application navigation

The current dashboard is organized into five persistent destinations:

1. **Overview** — local runtime readiness, current chat model, hardware summary, and clearly-labelled available/planned workspaces.
2. **Chat** — the primary user task, with a dedicated composer, incremental SSE token streaming, stop-generation behavior, and a bounded local conversation history. Conversations are stored in browser/WebView local storage under the per-user profile and are never uploaded or included in diagnostics.
3. **Models** — reviewed engines, models, dependencies, requirements, licenses, and installed state. The catalogue is still backed by reviewed built-in metadata; it is not yet a user-editable or remotely executable manifest.
4. **Activity** — installed engine state and the current download/update job snapshot. `GET /v1/activity` follows [activity-v1](../catalog/schemas/activity-v1.schema.json). It is a point-in-time snapshot, not a durable event history; events remain in local diagnostics until a separate event ledger is implemented.
5. **Settings** — language preference, release/update controls, advanced manual engine configuration, and privacy information.

The UI ships English and Turkish resource files under `web/locales/`. The loopback server serves only `/locales/en.json` and `/locales/tr.json`; arbitrary locale paths are not exposed. The current localization slice covers the navigation, section headings, and primary static content. Dynamic catalog, hardware, and error messages still need complete resource-key extraction before localization is considered complete.

## Separate tasks from components

- **Workspace** answers “What do I want to do?” Examples: chat, image generation, image understanding, transcription, speech, music/SFX, video, local knowledge search, and automation.
- **Engine** is an executable runtime or adapter that performs one or more tasks.
- **Model** is a versioned weight/checkpoint used by an engine; quantizations are distinct variants with their own resource and license metadata.
- **Dependency** is a package required by an engine (for example the official `cudart` DLL archive required beside a CUDA-enabled `llama-server`). Dependencies are installed and verified as part of the engine choice, not exposed as duplicate engines to users.
- **Installed instance** is a configured engine/model combination with process, readiness, and last-task status.
- **Job** describes download, verification, extraction, activation, update, or test progress. The current API exposes a point-in-time state/progress snapshot; the target model adds a durable phase/event trail, actionable error, and retry/cancel behavior where safe.
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

## Catalog navigation

The component library is a resource browser, not the primary list of user tasks. Its main filters should be:

1. **Engines** — runtime family, task capabilities, supported backend, platform, version, and required dependencies.
2. **Models** — task/modality, family, format, quantization/precision, language, model size, license, disk size, and estimated system RAM/VRAM.
3. **Supporting packages** — shown only where the user needs to understand a prerequisite; low-level CUDA DLL archives and similar assets remain dependencies of a selected engine.
4. **Installed** — installed version, source, verification status, assigned model, running/ready state, and actions.
5. **Activity & diagnostics** — one timeline for downloads, validation, engine start/stop, inference failures, update and rollback events.

Search should match task, model family, quantization, backend, hardware requirement, and license. Incompatible items should be hidden from the default recommendation view or disabled with a concise reason, while advanced users may inspect the full catalog.

## Versioned component metadata contract

Every catalog API entry must follow the published [component-v1 schema](../catalog/schemas/component-v1.schema.json), which is validated against the actual `/v1/catalog` response in HTTP smoke tests. The current contract includes the following fields; future versions may add requirements but must not silently change v1 semantics:

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

1. Establish the five-area UI shell, task-first Overview, dedicated Chat, and initial EN/TR resources (implemented as the first foundation slice).
2. Unify current operation state in the `activity-v1` snapshot and evolve it into a durable job/event contract with safe retry/cancel semantics.
3. Complete dynamic localization, accessible names/status announcements and first-run onboarding. Streaming Chat and bounded local conversation persistence are now implemented; regenerate, system instructions, model selection and richer history management remain future Chat improvements.
4. Move reviewed model, engine and dependency definitions to a checked-in versioned authoring manifest that generates both API metadata and the install allowlist from one source.
5. Finalize and test the shared engine adapter lifecycle against the existing `llama.cpp` integration.
6. Validate reproducible CPU/GPU hardware profiles and add Qwen3.5/Gemma 4 as model-family pilots on the existing engine where formats and capabilities are verified.
7. Add additional engine families only after the product, adapter, catalogue, compatibility, distribution and recovery gates are met.
