# Local AI Runtime

[![Pipeline](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml)

**A local-first, hardware-aware control plane for existing AI engines and models.**

Project site: **https://azmisahin-ai.github.io/local-ai-runtime/** (available after GitHub Pages is enabled and the first deploy succeeds)

This is an early C++20 alpha. It orchestrates existing model engines; it does not implement inference itself. The product name and API are still evolving.

> **Product status:** `v0.1.21-alpha` is the current published reference release. It includes the Windows per-user desktop installer, verified in-app update staging, embedded component manifest, persistent Activity and FIFO install queue, Runtime Profiles, task-first Models UI and reviewed multi-model pinned-GGUF catalog contract. The current shipped catalog still contains the reviewed Qwen starter plus three llama.cpp variants. Real GPU inference evidence, native screen-reader acceptance, publisher signing, cross-platform update parity and additional engine/workspace families remain open. The next milestone is the Shared Engine Adapter contract, with llama.cpp remaining the first built-in adapter. See the [roadmap](docs/roadmap.md).

## For users

### Downloads

Use the project site or [GitHub Releases](https://github.com/azmisahin-ai/local-ai-runtime/releases). The tagged pipeline builds a Windows x64 setup installer and portable ZIP, Linux x64, macOS Intel (x64), and macOS Apple Silicon (arm64) packages. Releases include `SHA256SUMS.txt`.

The runtime includes the C++ control plane, HTML dashboard, example configuration, launcher/update helpers, and license. It does not bundle inference engines, CUDA runtimes, or multi-gigabyte model weights. The dashboard is organized around **Overview**, **Chat**, **Models**, **Activity**, and **Settings**. Install reviewed engine/model components separately from **Models** so versions and licenses remain independently tracked. Component installs run through a persistent FIFO queue (one install worker at a time); repeated clicks reuse the existing queued job, and Activity shows queue position plus reviewed dependencies. Overview shows locally detected OS/architecture, logical CPU count, system/available RAM, free disk space and graphics adapters where platform probes support them. Activity adds live system CPU/RAM and managed-engine process usage; NVIDIA utilization/VRAM is shown only when `nvidia-smi` is available and is system-wide, not per-engine evidence. This data is not uploaded. Detecting a GPU or installing a CUDA package does not prove that inference used the GPU; the driver, backend, model and context still need a real-task check.

- **Windows 10/11 x64 — desktop installer (recommended):** download `local-ai-runtime-windows-x64-setup.exe` from the [latest release](https://github.com/azmisahin-ai/local-ai-runtime/releases) and run setup. It installs per-user without administrator elevation, adds a Start menu shortcut and offers an optional desktop shortcut. Open **Local AI Runtime** from Start; the dashboard opens in its own desktop window without a terminal. The current alpha installer is unsigned and may show a Windows publisher/SmartScreen warning; verify it against that release's `SHA256SUMS.txt`. Do not disable Windows protection to bypass the warning. Setup installs Microsoft's WebView2 Runtime if needed, which can require internet access.
- **Windows portable fallback:** when a release has no setup installer, download the ZIP, extract it to a folder you own, and double-click `Start-LocalAIRuntime.cmd`. The dashboard opens in your browser; this is the interim alpha path.
- **Linux x64:** extract the TAR.GZ and run `Start-LocalAIRuntime.sh`. The dashboard opens in your browser; press Ctrl+C in the terminal to stop the runtime.
- **macOS Intel / Apple Silicon:** extract the matching architecture archive and double-click `Start-LocalAIRuntime.command`. This early CLI preview is not Developer ID signed or notarized, so Gatekeeper/security prompts may occur.

The desktop app's close button minimizes it to the notification area. Choose **Exit and stop local engines** from the tray menu to stop the runtime and managed engines gracefully. Settings, downloaded engines/models and logs are stored under `%LOCALAPPDATA%\LocalAIRuntime`, outside the program directory, so they survive app updates/uninstall by default. For installed Windows desktop builds, use **Check for updates** and follow the in-app download/install confirmation. The full setup package is verified before apply; the updater makes a whole-install backup and rolls back if the new shell/runtime fails its health check. Linux/macOS currently stage verified update archives but still require replacing the archive manually.

#### First model setup

1. Open Local AI Runtime and go to **Models**.
2. Install the reviewed llama.cpp CPU engine. Review the upstream MIT license and confirm it in the UI.
3. Install the starter Qwen2.5 1.5B Instruct Q4_K_M model. Review its Apache-2.0 license and confirm it in the UI.
4. In **Models**, select the installed llama.cpp CPU package and the installed Qwen model, choose a context size, and apply the Runtime Profile.
5. Start the selected engine profile. Wait until the engine status says **Ready**, then send a message in **Chat with your model**.

The starter model download is about 1.12 GB. The lightweight runtime setup does not contain the engine or model. If upstream has no verified asset matching your platform, the catalog fails closed rather than bypassing checksum validation. The current catalogue is a reviewed, build-time embedded manifest (`catalog/manifest-v1.json`); it is a starter catalogue, not yet a general-purpose marketplace. New models must be pinned and reviewed before they become installable.

Advanced users can still use **Configure llama.cpp** to register engine/model files they installed themselves. NVIDIA CUDA packages are separate choices on supported Windows/Linux x64 hosts; the catalog verifies the server archive and matching CUDA runtime/DLL archive as a pair. Package installation is not proof that a specific GPU, driver, VRAM and model combination works. Start with CPU, then validate CUDA on the exact machine and model. Use **Export diagnostics** if installation or engine startup fails; the report includes the job phase, selected package names/hashes and failure reason without automatically uploading anything.

- **Offline use:** after the runtime, engine and model have been installed, inference runs locally without GitHub. Catalog downloads and update checks require network access. Prompts, model paths and usage events are not uploaded; ordinary network metadata may be visible to download sources.

### Security warning

The API has no authentication. It binds to `127.0.0.1` and should not be exposed directly to a LAN or the public internet. A process running on the same computer can call the API. Do not run packages from untrusted sources.

## For developers

### Prerequisites

- CMake 3.24+
- C++20 compiler (Visual Studio Build Tools with **Desktop development with C++** on Windows; GCC/Clang on Unix)
- Git
- Python 3.10+ for the HTTP end-to-end smoke test
- vcpkg on Windows (the helper bootstraps a local copy on the first run); Boost 1.74+, libcurl, libarchive and OpenSSL development packages on Unix

### Build from source

**Windows PowerShell** (from repository root):

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1
```

**Ubuntu/Debian:**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libboost-dev libcurl4-openssl-dev libarchive-dev libssl-dev python3
./scripts/build.sh
```

Or configure manually on Unix:

```bash
cmake -S . -B build -DLAR_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The Windows helper configures vcpkg manifest mode. Do not first configure the same build directory without the vcpkg toolchain and then reuse it; the helper detects and clears a stale generated build tree when needed.

### Run the development build

From repository root, after building:

```powershell
# Windows
.\build\Release\local-ai-runtime.exe
```

```bash
# Linux/macOS
./build/local-ai-runtime
```

Open http://127.0.0.1:8000. Use **Models** for the verified llama.cpp CPU/CUDA packages and the pinned starter GGUF, or use **Configure llama.cpp** if you already have engine/model files. The managed catalog requires explicit license acknowledgement and only accepts reviewed component IDs from the embedded manifest. Installations enter a persistent FIFO queue with one installer worker; duplicate requests reuse the existing job, and Activity reports the queue position and any reviewed runtime dependency.

## CI and releases

- [Canonical pipeline](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml): one workflow orchestrates Windows/MSVC, Ubuntu/GCC and macOS Apple Clang on Intel and Apple Silicon. Pull requests build and test without packaging; each `main` push builds/tests/packages the four release targets in parallel, smoke-tests the extracted packages, and **only after all platform jobs pass** publishes the website as the final job. Commit-specific preview archives are retained for 14 days. Pushing a `v*` tag runs the same platform jobs and publishes a GitHub Release with Windows, Linux and both macOS architectures plus SHA-256 checksums only after every package passes. A weekly scheduled run performs only the macOS compatibility gate; a manual run performs a versioned rehearsal without creating a Release. Enable **Settings → Pages → Source → GitHub Actions** once.
- Maintainers can use the fixed root-level [publish helper](Publish-LocalAIRuntime.ps1) and replace only `local-ai-runtime-update.bundle` for each prepared change. The bundle preserves the exact commit hash prepared for that release. It pushes without force, waits for the four-platform `main` workflow, then tags and waits for the versioned Release. The same downloaded command calls the release-gate code included in each prepared commit. See [release and update process](docs/release-and-update.md).

GitHub Actions is the source for repeatable cross-platform build/test results. Local hardware/model checks are separate, and no NVIDIA/CUDA combination is claimed supported until that exact configuration is tested and recorded.

## Current feature slice

- C++20 core, CMake builds, JSON engine manifest.
- Start/stop explicitly configured child processes without shell command concatenation.
- Per-engine output/error logs and basic process status.
- Loopback-only HTTP API and same-origin dashboard.
- Non-streaming OpenAI-compatible chat proxy and `/v1/models` forwarding.
- `POST /v1/setup/llama-cpp` to validate and save local `llama-server`/GGUF paths, backend mode, context size and engine port.
- `/v1/engines` reports process state and API readiness separately; the dashboard does not enable chat until the default engine is ready.
- `POST /v1/runtime/shutdown` is a loopback-only graceful shutdown used by the Windows launcher when replacing an older runtime instance; managed engine processes are stopped as the runtime exits.
- `/version` and `/health` endpoints; dashboard offers official per-platform runtime update staging with size and SHA-256 verification, plus user-confirmed apply/restart and automatic rollback for tagged Windows portable releases. Linux/macOS and development builds remain stage-only.
- `/v1/system/identity` returns an opaque persistent user-data-root ID (never an absolute path). The Windows desktop shell, launcher, runtime catalogue/download state and updater pass the same explicit data-root contract; a healthy but stale runtime on the selected port is not silently treated as the current instance.
- Built-in managed component catalog for the official llama.cpp release (CPU/CUDA variants) and a pinned Qwen2.5 1.5B GGUF starter model. Downloads are cancellable, size-bounded, HTTPS-only, SHA-256 checked, and safely extracted; model license confirmation is required.
- `/v1/catalog`, `/v1/downloads`, per-job progress/cancel routes, `/v1/activity` snapshot API and dashboard installation progress. The activity API is a current snapshot, not yet a durable event history.
- Five-area UI navigation, first-pass English/Turkish static resource files and a locally persisted language preference. Dynamic catalogue/hardware/error strings still need full localization.
- Local-only rotating runtime event log, user-initiated diagnostic export (download jobs and engine status), and optional user-consented engine log excerpts.
- Fake-engine core and HTTP end-to-end tests; one required Windows/Linux/macOS x64/arm64 CI workflow. CI validates the installer API without downloading external binaries or model weights.
- Public project page and tag-driven Windows/Linux/macOS release pipeline.

Still in progress: complete resource-key localization, streaming chat and conversation history, a manifest-driven extensible catalogue, full engine adapter contracts, stronger hardware/backend compatibility evidence, signed binaries, Linux/macOS desktop update parity, and audio/vision/embedding adapters. The managed catalogue currently contains a reviewed built-in allowlist for llama.cpp CPU/CUDA packages and one pinned GGUF model; it does not yet support arbitrary model families or prove every binary works with every real model/platform combination. See [roadmap](docs/roadmap.md) and [diagnostics guide](docs/diagnostics.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [diagnostics and troubleshooting](docs/diagnostics.md), [architecture](docs/architecture.md), [engine/model distribution strategy](docs/engine-distribution.md), [release/update process](docs/release-and-update.md), [MCP plan](docs/mcp-integration.md), and [security policy](SECURITY.md). Update `site/product.json` when user-visible features or their status change. Add regression tests for behavior changes.


### Windows PowerShell 5.1 publishing

The stable `Publish-LocalAIRuntime.ps1` script is compatible with Windows PowerShell 5.1. Git commands are checked by their exit codes rather than treating successful stderr diagnostics (such as `git bundle verify` reporting that the bundle is okay) as failures. If an older publisher stops with `NativeCommandError`, replace both `Publish-LocalAIRuntime.ps1` and `local-ai-runtime-update.bundle` with the matching files from the latest publisher kit, then rerun the same command. The bootstrap checks the bundle and will not force-push or overwrite local work.

## Engine integration architecture

The runtime uses a reviewed Shared Engine Adapter contract. The first built-in adapter is `llama.cpp`; other local OpenAI-compatible test/process configurations use a compatibility base adapter. This does not mean Ollama, ONNX Runtime or media engines are shipped as supported products yet. See the [Engine Adapter architecture](docs/engine-adapter-architecture.md) and [roadmap](docs/roadmap.md).
