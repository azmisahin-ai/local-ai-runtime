# Engine and model distribution strategy

The Local AI Runtime binary, inference engines, and model weights are separate products with different release cycles and licenses. The runtime must remain small and usable without bundling multi-gigabyte weights.

## Current implementation and planned user flow

The current preview implements a built-in, reviewed catalog through `/v1/catalog`, download jobs through `/v1/downloads`, and dashboard progress/cancel controls. It includes official llama.cpp CPU/CUDA asset discovery from the GitHub latest-stable release API and one pinned Qwen2.5 1.5B Q4_K_M GGUF model. The first download requires explicit upstream license acknowledgement; arbitrary URLs are not accepted. This implementation is a starter catalog, not yet a general-purpose registry format for every engine family.

The product flow is:

1. The runtime detects the current OS/architecture and exposes only platform/backend combinations the current asset selector supports. Hardware/driver detection and performance-backed recommendations are not implemented yet.
2. The user chooses a catalog item, reviews its upstream license, and confirms the download. A download is not started when consent is missing.
3. Downloads use HTTPS-only redirects with TLS validation, byte limits, cancellation, staging paths and SHA-256 checks. The model is also checked for exact byte size and GGUF magic. Engine archives are extracted only when entries are regular files/directories with safe relative paths; symlinks, hardlinks and special files are rejected.
4. Verified files are placed in the per-user Local AI Runtime data directory, and the first `llama.cpp` engine is registered into the user's runtime config. If configuration registration fails, the newly activated engine directory is rolled back. Windows uses `%LOCALAPPDATA%\LocalAIRuntime`; Linux follows `$XDG_DATA_HOME/local-ai-runtime` or `~/.local/share/local-ai-runtime`; macOS follows `$XDG_DATA_HOME/local-ai-runtime` or `~/Library/Application Support/LocalAIRuntime`. Config, engine/model files and logs live outside the extracted program directory so replacing the app bundle does not overwrite user data.
5. CI validates the catalog schema, rejection paths, API behavior and package builds without downloading real external executables or the 1.12 GB model. A real internet download and real `llama-server`/GGUF inference test remain required before declaring this integration fully verified on a user's machine.
6. Future catalog entries must include the exact source, immutable revision where possible, license, size, SHA-256, supported platforms/backends, prerequisites, known limitations and explicit evidence status. Signature/provenance checks should be added when upstream supports them; SHA-256 from the same upstream API is an integrity control, not publisher authentication.

## First engine candidate: llama.cpp

The official releases page currently publishes distinct platform/backend builds, including Windows x64 CPU, CUDA 12, CUDA 13 and Vulkan variants. These should be catalogued as distinct artifacts rather than treated as one universal executable: <https://github.com/ggml-org/llama.cpp/releases>.

The first integration should use the upstream `llama-server` OpenAI-compatible HTTP interface behind the existing process adapter. Start with a CPU path and a user-selected/verified backend. Do not claim CUDA support until the exact downloaded artifact, NVIDIA driver, GPU, model format and test succeed.

## Model and engine catalog record

The initial catalog schema should include at least:

- `engine_id`, `engine_version`, `source_project`, `source_release_url`, `source_commit`
- `os`, `architecture`, `backend`, `minimum_driver` and runtime prerequisites
- `artifact_url`, `artifact_size`, `sha256`, signature/provenance metadata when available
- `license_id`, `license_url`, `redistribution_policy`, `tested_status`, `tested_on`
- `capabilities`, `known_limitations`, `release_channel`

Model records additionally need format, model revision, author/source, license terms, size, checksum, context and hardware notes. Model files should not be committed into this repository or copied into runtime releases.

## RTX 3060 test profile

For the first local GPU validation, record the exact Windows build, NVIDIA driver, GPU model/VRAM, system RAM, llama.cpp asset/version, selected backend, model name/revision, quantization, context size, and whether the test passed. Start with a small quantized model and a modest context. A 6 GB VRAM limit means the UI must present memory expectations and may need a CPU/RAM fallback; the runtime should not promise that an arbitrary model fits.

## Release policy

- Runtime releases version the control plane only.
- Engine packages have their own version and backend compatibility metadata.
- Models are independently selected and licensed.
- Roll back the engine package independently of runtime/config/model data.
- Never execute install scripts supplied by model repositories.
