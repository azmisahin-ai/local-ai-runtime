# Local AI Runtime

[![Pipeline](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pipeline.yml)

**A local-first, hardware-aware control plane for existing AI engines and models.**

Project site: **https://azmisahin-ai.github.io/local-ai-runtime/** (available after GitHub Pages is enabled and the first deploy succeeds)

This is an early C++20 alpha. It orchestrates existing model engines; it does not implement inference itself. The product name and API are still evolving.

## For users

### Downloads

Use the project site or [GitHub Releases](https://github.com/azmisahin-ai/local-ai-runtime/releases). After a version tag is published, GitHub Actions builds packages for Windows x64, Linux x64, macOS Intel (x64) and macOS Apple Silicon (arm64), then attaches `SHA256SUMS.txt`.

The package includes the runtime, the web dashboard, an example configuration, a starter script, and the project license. It deliberately does not bundle inference engines, CUDA runtimes, or multi-gigabyte model weights. After launching the runtime, use **Managed Components** in the dashboard to download reviewed components on demand; each engine/model remains separate so versions and licenses can be tracked independently.

- **Windows 10/11 x64:** extract the ZIP and double-click `Start-LocalAIRuntime.cmd`. The dashboard opens in your browser. If startup fails, the console stays open so you can read the message.
- **Linux x64:** extract the TAR.GZ and run `./Start-LocalAIRuntime.sh`. A desktop session opens the dashboard automatically; on a headless/server session, open `http://127.0.0.1:8000` yourself. Press Ctrl+C in the terminal to stop the runtime.
- **macOS Intel / Apple Silicon:** extract the matching architecture archive and double-click `Start-LocalAIRuntime.command`. The terminal stays open and the dashboard opens in your browser. This early CLI preview is not Developer ID signed or notarized, so Gatekeeper/security prompts may occur. Only open software obtained from the official project release.

The launchers create user configuration on first start. Settings, downloaded engines/models and logs are stored outside the extracted application folder, so they survive replacing the app package. The runtime package does not include an inference engine or multi-gigabyte model weights.

#### First model setup

1. Start Local AI Runtime and open its local dashboard at `http://127.0.0.1:8000`.
2. In **Managed Components**, begin with the llama.cpp CPU package. Review the upstream MIT license, confirm it in the UI, and let the runtime download, checksum-verify, safely extract and register it.
3. Install the starter Qwen2.5 1.5B Instruct Q4_K_M model. Review its Apache-2.0 license, confirm it, and let the runtime download the pinned GGUF, check its exact size/SHA-256/header, and register it.
4. Wait for both jobs to complete, start the engine and send a message when the dashboard reports it ready.

The initial model download is about 1.12 GB and requires an internet connection plus additional free disk space. The lightweight runtime release does not contain the engine archive or model weights. The catalog selects reviewed upstream URLs; arbitrary URLs are not accepted by the installer API. If the latest upstream engine release lacks a published SHA-256 digest or a matching platform/backend asset, installation fails closed rather than bypassing verification.

You do not need a developer test script to verify a normal installation: when the engine is marked **Ready**, send a message in **Chat with your model**. The app sends the prompt to the configured local engine and shows the response in the dashboard. Maintainer-only real-device validation is documented separately in [real-engine Windows validation](docs/real-engine-validation.md).

Advanced users can still use **Configure llama.cpp** to register an engine and model they already installed. NVIDIA CUDA packages are available as separate choices on supported Windows/Linux x64 hosts, but a package option is not proof that a given GPU/driver/model combination works. Start with CPU, then test CUDA on the exact machine and model.

- **Offline use:** once the app, engine and model are installed, local inference does not need GitHub. Catalog downloads and update checks require network access. The dashboard does not upload prompts, model paths or usage events; the source hosts may receive ordinary network metadata such as the request IP address.

The local dashboard checks GitHub Releases at startup, provides **Check for updates**, and refreshes the check while the dashboard remains open. When a newer version is available, it offers release notes plus a **Download & verify package** action. The runtime restricts the request to the exact official release asset, checks the reported size and SHA-256 digest, and stages the verified archive under the per-user update directory. On tagged portable Windows releases, a second explicit **Apply update & restart** action revalidates the package, checks that the portable app folder is writable before stopping anything, backs up application files, restarts the runtime, and rolls back if the new version fails its health check. If an update helper is interrupted during replacement or rollback, the next launcher start verifies the saved backup and attempts recovery before executing the runtime. It does not request administrator elevation; if the app is under a protected folder, move the portable folder to a user-writable location or update manually. This action is disabled in development/preview builds to avoid replacing newer preview code with an older tagged release. Linux and macOS currently support staging only. The rollback copy is kept under the per-user updates folder; user settings, engines and model weights remain outside the application folder.



**One-time transition from `v0.1.0-alpha`:** the first published archive predates the new top-level click-to-start launcher and its updater does not allow that new file. Install the next launcher-enabled release manually into a new application folder once; your settings, engines and models stay in the separate user-data folder. Later Windows tagged releases can use the in-app apply/restart flow.

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
