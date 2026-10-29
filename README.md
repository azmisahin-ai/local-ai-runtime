# Local AI Runtime

[![Pipeline](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml)

**A local-first, hardware-aware control plane for existing AI engines and models.**

Project site: **https://azmisahin-ai.github.io/local-ai-runtime/** (available after GitHub Pages is enabled and the first deploy succeeds)

This is an early C++20 alpha. It orchestrates existing model engines; it does not implement inference itself. The product name and API are still evolving.

> **Product status:** this branch builds a Windows per-user setup installer and an embedded desktop window. The installer is publicly available only for release tags whose CI run publishes `local-ai-runtime-windows-x64-setup.exe`; check the [live Releases page](https://github.com/azmisahin-ai/local-ai-runtime/releases) for the exact assets available today. Hardware-aware recommendations, broader engine support, signed binaries, and atomic in-app desktop updates remain in progress. See the [roadmap](docs/roadmap.md).

## For users

### Downloads

Use the project site or [GitHub Releases](https://github.com/azmisahin-ai/local-ai-runtime/releases). The tagged pipeline builds a Windows x64 setup installer and portable ZIP, Linux x64, macOS Intel (x64), and macOS Apple Silicon (arm64) packages. Releases include `SHA256SUMS.txt`.

The runtime includes the C++ control plane, HTML dashboard, example configuration, launcher/update helpers, and license. It does not bundle inference engines, CUDA runtimes, or multi-gigabyte model weights. Install reviewed engine/model components separately from **Managed Components** so versions and licenses remain independently tracked.

- **Windows 10/11 x64 — desktop installer (recommended):** download `local-ai-runtime-windows-x64-setup.exe` from the [latest release](https://github.com/azmisahin-ai/local-ai-runtime/releases) and run setup. It installs per-user without administrator elevation, adds a Start menu shortcut and offers an optional desktop shortcut. Open **Local AI Runtime** from Start; the dashboard opens in its own desktop window without a terminal. The current alpha installer is unsigned and may show a Windows publisher/SmartScreen warning; verify it against that release's `SHA256SUMS.txt`. Do not disable Windows protection to bypass the warning. Setup installs Microsoft's WebView2 Runtime if needed, which can require internet access.
- **Windows portable fallback:** when a release has no setup installer, download the ZIP, extract it to a folder you own, and double-click `Start-LocalAIRuntime.cmd`. The dashboard opens in your browser; this is the interim alpha path.
- **Linux x64:** extract the TAR.GZ and run `Start-LocalAIRuntime.sh`. The dashboard opens in your browser; press Ctrl+C in the terminal to stop the runtime.
- **macOS Intel / Apple Silicon:** extract the matching architecture archive and double-click `Start-LocalAIRuntime.command`. This early CLI preview is not Developer ID signed or notarized, so Gatekeeper/security prompts may occur.

The desktop app's close button minimizes it to the notification area. Choose **Exit and stop local engines** from the tray menu to stop the runtime and managed engines gracefully. Settings, downloaded engines/models and logs are stored under `%LOCALAPPDATA%\LocalAIRuntime`, outside the program directory, so they survive app updates/uninstall by default. For installed desktop builds, use **Check for updates** to open the official release page, exit the app from the tray, then run the complete setup installer; the portable ZIP updater is deliberately disabled for desktop installs until atomic shell/runtime updates are implemented. Linux/macOS currently stage verified update archives but still require replacing the archive manually.

#### First model setup

1. Open Local AI Runtime and go to **Managed Components**.
2. Install the reviewed llama.cpp CPU engine. Review the upstream MIT license and confirm it in the UI.
3. Install the starter Qwen2.5 1.5B Instruct Q4_K_M model. Review its Apache-2.0 license and confirm it in the UI.
4. Wait until the engine status says **Ready**, then send a message in **Chat with your model**.

The starter model download is about 1.12 GB. The lightweight runtime setup does not contain the engine or model. If upstream has no verified asset matching your platform, the catalog fails closed rather than bypassing checksum validation.

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

Open http://127.0.0.1:8000. Use **Managed Components** for the verified llama.cpp CPU/CUDA packages and the pinned starter GGUF, or use **Configure llama.cpp** if you already have engine/model files. The managed catalog requires explicit license acknowledgement and only accepts built-in reviewed item IDs.

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
- Built-in managed component catalog for the official llama.cpp release (CPU/CUDA variants) and a pinned Qwen2.5 1.5B GGUF starter model. Downloads are cancellable, size-bounded, HTTPS-only, SHA-256 checked, and safely extracted; model license confirmation is required.
- `/v1/catalog`, `/v1/downloads`, per-job progress/cancel routes and dashboard installation progress.
- Local-only rotating runtime event log, user-initiated diagnostic export (download jobs and engine status), and optional user-consented engine log excerpts.
- Fake-engine core and HTTP end-to-end tests; one required Windows/Linux/macOS x64/arm64 CI workflow. CI validates the installer API without downloading external binaries or model weights.
- Public project page and tag-driven Windows/Linux/macOS release pipeline.

Not implemented yet: comprehensive hardware/backend detection and recommendations, unattended self-updates, platform installers, audio/vision/embedding adapters, streaming chat, authentication and MCP. The managed catalog downloads the first llama.cpp package and one pinned GGUF model, but it does not yet automatically install every engine/model family or prove the binary works with a real model on every platform. See [roadmap](docs/roadmap.md) and [diagnostics guide](docs/diagnostics.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [diagnostics and troubleshooting](docs/diagnostics.md), [architecture](docs/architecture.md), [engine/model distribution strategy](docs/engine-distribution.md), [release/update process](docs/release-and-update.md), [MCP plan](docs/mcp-integration.md), and [security policy](SECURITY.md). Update `site/product.json` when user-visible features or their status change. Add regression tests for behavior changes.


### Windows PowerShell 5.1 publishing

The stable `Publish-LocalAIRuntime.ps1` script is compatible with Windows PowerShell 5.1. Git commands are checked by their exit codes rather than treating successful stderr diagnostics (such as `git bundle verify` reporting that the bundle is okay) as failures. If an older publisher stops with `NativeCommandError`, replace both `Publish-LocalAIRuntime.ps1` and `local-ai-runtime-update.bundle` with the matching files from the latest publisher kit, then rerun the same command. The bootstrap checks the bundle and will not force-push or overwrite local work.
