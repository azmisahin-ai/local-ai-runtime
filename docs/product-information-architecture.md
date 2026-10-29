# Product information architecture

This document defines the user-facing organization of Local AI Runtime. The dashboard must remain understandable as the number of engines and model families grows; implementation details and download artifacts are not the primary navigation model.

## Separate tasks from components

- **Workspace** answers “What do I want to do?” Examples: chat, image generation, image understanding, transcription, speech, music/SFX, video, local knowledge search, and automation.
- **Engine** is an executable runtime or adapter that performs one or more tasks.
- **Model** is a versioned weight/checkpoint used by an engine; quantizations are distinct variants with their own resource and license metadata.
- **Dependency** is a package required by an engine (for example the official `cudart` DLL archive required beside a CUDA-enabled `llama-server`). Dependencies are installed and verified as part of the engine choice, not exposed as duplicate engines to users.
- **Installed instance** is a configured engine/model combination with process, readiness, and last-task status.
- **Job** describes download, verification, extraction, activation, update, or test progress. It has a durable phase, event trail, actionable error, and retry/cancel behavior where safe.
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

Every catalog entry should eventually provide the following fields (schema versioned and validated in CI):

- Identity: `schema_version`, stable `id`, `kind`, `name`, `family`, `version`/version policy.
- Purpose: `workspaces`, `tasks`, `modalities`, `input_formats`, `output_formats`, and capabilities.
- Compatibility: OS, architecture, CPU feature requirements, backend (`cpu`, `cuda`, `vulkan`, `metal`, etc.), driver/runtime requirements and known exclusions.
- Resources: download size, installed size, minimum/recommended system RAM, minimum/recommended VRAM, disk-space overhead, expected context/working-memory impact where known.
- Provenance: canonical HTTPS source, immutable release/revision, published digest/signature, license identifier and exact license URL, plus dependency IDs.
- Evidence: package hash verified, install/probe status, process-ready status, real-task test status, test date, software versions, and hardware profile identifier.
- Lifecycle: install directory policy, configuration adapter, readiness probe, start/stop semantics, log locations, uninstall behavior, update/rollback policy.

No arbitrary catalog URL may be accepted by the runtime API. Model repositories must not be allowed to contribute scripts that the application executes. Packages are resolved against official metadata/pinned revisions, checksummed, safely extracted, and registered only after the complete dependency set verifies.

## Evidence states

Show these states separately; never collapse them into one “supported” badge:

1. `Listed` — reviewed metadata exists.
2. `Package verified` — selected official artifact(s) match size/hash/provenance policy.
3. `Installed` — files were extracted and registered.
4. `Process ready` — executable starts and its health/model API answers.
5. `Task tested` — a real task completed successfully for a recorded model/backend.
6. `Profile validated` — repeated task, memory, startup and shutdown checks passed on a named OS/driver/hardware profile.

A CUDA package being downloadable or a GPU being present does not mean inference used the GPU. Report selected backend, GPU detection evidence, driver/runtime check, offloaded layers when available, VRAM observation where available, and CPU fallback explicitly.

## Diagnostics and support

Every job emits structured local events with `job_id`, `component_id`, stage/phase, timestamp, progress bytes, attempt count, error code, sanitized actionable message, and recovery hint. Preserve the selected upstream asset names and hashes in the diagnostic report. Never log prompts or generated text in runtime-owned events. Engine logs remain opt-in because upstream runtimes may log prompt fragments or local paths. Diagnostics are local-only and user-exported; nothing is uploaded automatically.

## Implementation order

1. Keep the released Windows installer and existing CPU chat path reliable.
2. Pair and SHA-256-verify the official CUDA server + companion runtime archives; expose the two asset names/hashes in local diagnostics. Run a real task on a physical GPU and collect backend/VRAM evidence before advertising CUDA as tested.
3. Add local hardware discovery and memory estimates; use the detected profile to order/disable catalog choices with explanations.
4. Finalize the common engine/model/dependency schema and lifecycle contract against `llama.cpp`.
5. Add the next runtime family only when packaging, process lifecycle, logs, cancel/retry, uninstall, update and real-task tests pass for that family.
6. Implement the same product model for image, audio, music/SFX, video, knowledge/search and automation, while keeping each domain's unsupported capabilities visibly planned.
