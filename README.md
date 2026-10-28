# Local AI Runtime

[![CI](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml)
[![Pages](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pages.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pages.yml)

**A local-first, hardware-aware control plane for existing AI engines and models.**

Project site: **https://azmisahin-ai.github.io/local-ai-runtime/** (available after GitHub Pages is enabled and the first deploy succeeds)

This is an early C++20 alpha. It orchestrates existing model engines; it does not implement inference itself. The product name and API are still evolving.

## For users

### Downloads

Use the project site or [GitHub Releases](https://github.com/azmisahin-ai/local-ai-runtime/releases). After a version tag is published, GitHub Actions builds portable packages for Windows x64 and Linux x64 and attaches `SHA256SUMS.txt`.

The package includes the runtime, the web dashboard, an example configuration, a starter script, and the project license. It deliberately does not bundle inference engines, CUDA runtimes, or multi-gigabyte model weights. After launching the runtime, use **Managed Components** in the dashboard to download reviewed components on demand; each engine/model remains separate so versions and licenses can be tracked independently.

- **Windows 10/11 x64:** extract the ZIP, open PowerShell in the extracted folder, and run:

  ```powershell
  Set-ExecutionPolicy -Scope Process Bypass
  .\scripts\Start-LocalAIRuntime.ps1
  ```

  The script creates the user configuration on first start and opens the dashboard. Settings, downloaded engines/models and logs are stored under `%LOCALAPPDATA%\LocalAIRuntime`, outside the extracted application folder, so they survive replacing the app package.
- **Linux x64:** extract the TAR.GZ and run `./scripts/start-runtime.sh`. The runtime stays in the foreground; press Ctrl+C to stop. On Linux, user data is stored in `$XDG_DATA_HOME/local-ai-runtime` when set, otherwise `~/.local/share/local-ai-runtime`. On macOS developer runs, it defaults to `~/Library/Application Support/LocalAIRuntime` (or follows `XDG_DATA_HOME` when set).

#### First model setup

1. Start Local AI Runtime and open its local dashboard at `http://127.0.0.1:8000`.
2. In **Managed Components**, begin with the llama.cpp CPU package. Review the upstream MIT license, confirm it in the UI, and let the runtime download, checksum-verify, safely extract and register it.
3. Install the starter Qwen2.5 1.5B Instruct Q4_K_M model. Review its Apache-2.0 license, confirm it, and let the runtime download the pinned GGUF, check its exact size/SHA-256/header, and register it.
4. Wait for both jobs to complete, start the engine and send a message when the dashboard reports it ready.

The initial model download is about 1.12 GB and requires an internet connection plus additional free disk space. The lightweight runtime release does not contain the engine archive or model weights. The catalog selects reviewed upstream URLs; arbitrary URLs are not accepted by the installer API. If the latest upstream engine release lacks a published SHA-256 digest or a matching platform/backend asset, installation fails closed rather than bypassing verification.

Advanced users can still use **Configure llama.cpp** to register an engine and model they already installed. NVIDIA CUDA packages are available as separate choices on supported Windows/Linux x64 hosts, but a package option is not proof that a given GPU/driver/model combination works. Start with CPU, then test CUDA on the exact machine and model.

- **Offline use:** once the app, engine and model are installed, local inference does not need GitHub. Catalog downloads and update checks require network access. The dashboard does not upload prompts, model paths or usage events; the source hosts may receive ordinary network metadata such as the request IP address.

The local dashboard checks for a newer **stable** release on startup and links to release notes/downloads. This alpha does **not** install or replace binaries silently. Review the package and release notes before updating.

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

- [CI runs](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml): Windows/MSVC and Ubuntu/GCC run on pull requests and pushes to non-main branches. Pushes to `main` are built and tested by the package workflow below, so the same C++ targets are not rebuilt in two workflows for each main commit. Experimental macOS Intel compatibility runs weekly or manually in the separate [macOS workflow](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/macos-compat.yml); it does not produce a macOS release package yet. CTest uses a fake engine and does not download models or require a GPU.
- [Website deployment](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pages.yml): pushes to `main` that change `site/` deploy it using GitHub Pages; source-only changes do not trigger a needless redeploy. Enable **Settings → Pages → Source → GitHub Actions** once.
- [Preview/release workflow](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/release.yml): this is the single canonical build/test/package pipeline for pushes to `main`; it compiles once per target OS, runs CTest, validates the web assets/manifests, packages, extracts, and smoke-tests Windows/Linux preview builds. The ZIP/TAR.GZ are uploaded as run artifacts for 14 days. A separate Pages workflow deploys only the static website. A manual run performs a versioned rehearsal without publishing. Pushing a `v*` tag repeats the build/package checks, generates SHA-256 checksums, and publishes a GitHub Release.
- See [release and update process](docs/release-and-update.md) before publishing a tag.

GitHub Actions is the source for repeatable cross-platform build/test results. Local hardware/model checks are separate, and no NVIDIA/CUDA combination is claimed supported until that exact configuration is tested and recorded.

## Current feature slice

- C++20 core, CMake builds, JSON engine manifest.
- Start/stop explicitly configured child processes without shell command concatenation.
- Per-engine output/error logs and basic process status.
- Loopback-only HTTP API and same-origin dashboard.
- Non-streaming OpenAI-compatible chat proxy and `/v1/models` forwarding.
- `POST /v1/setup/llama-cpp` to validate and save local `llama-server`/GGUF paths, backend mode, context size and engine port.
- `/v1/engines` reports process state and API readiness separately; the dashboard does not enable chat until the default engine is ready.
- `/version` and `/health` endpoints; dashboard checks the public release feed for stable update notices.
- Built-in managed component catalog for the official llama.cpp release (CPU/CUDA variants) and a pinned Qwen2.5 1.5B GGUF starter model. Downloads are cancellable, size-bounded, HTTPS-only, SHA-256 checked, and safely extracted; model license confirmation is required.
- `/v1/catalog`, `/v1/downloads`, per-job progress/cancel routes and dashboard installation progress.
- Fake-engine core and HTTP end-to-end tests; required Windows/Linux CI plus scheduled/manual experimental macOS compatibility checks. CI validates the installer API without downloading external binaries or model weights.
- Public project page and tag-driven Windows/Linux release pipeline.

Not implemented yet: comprehensive hardware/backend detection and recommendations, unattended self-updates, platform installers, audio/vision/embedding adapters, streaming chat, authentication and MCP. The managed catalog downloads the first llama.cpp package and one pinned GGUF model, but it does not yet automatically install every engine/model family or prove the binary works with a real model on every platform. See [roadmap](docs/roadmap.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [architecture](docs/architecture.md), [engine/model distribution strategy](docs/engine-distribution.md), [release/update process](docs/release-and-update.md), [MCP plan](docs/mcp-integration.md), and [security policy](SECURITY.md). Update `site/product.json` when user-visible features or their status change. Add regression tests for behavior changes.
