# Engine and model distribution strategy

The Local AI Runtime binary, inference engines, and model weights are separate products with different release cycles and licenses. The runtime must remain small and usable without bundling multi-gigabyte weights.

## Proposed user flow

1. Detect the operating system, architecture, CPU capabilities, GPU/driver information when available, and free memory. Report what is detected separately from what has actually passed a test.
2. Show only catalog entries that match the OS/architecture. Present CPU as a fallback and list accelerator variants such as NVIDIA CUDA 12, NVIDIA CUDA 13, and Vulkan only when their package prerequisites are understood.
3. Clearly label the upstream project, exact engine version/commit, license, download size, backend, required driver/runtime, and test evidence. Do not silently select a new backend merely because a GPU is present.
4. Require explicit consent before downloading external executables. Only accept URLs from reviewed catalog entries; do not provide an arbitrary URL-to-executable feature.
5. Download to a staging directory, support cancellation/resume, verify the catalogued checksum, and verify publisher provenance/signatures when upstream provides them. A checksum hosted beside the same asset is useful for corruption detection but is not publisher authentication by itself.
6. Unpack with path-traversal protections, run a version probe, start a minimal health-check job, and only then mark that engine version as available. Keep the previously working version until the new version passes these checks.
7. Model selection is a separate flow. Show the model source, license, size, format, context/RAM/VRAM estimates and known limitations. Require license acknowledgement where appropriate. Preserve model paths and user configuration across runtime upgrades.

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
