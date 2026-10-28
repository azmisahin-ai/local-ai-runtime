# Local AI Runtime

[![CI](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml)
[![Pages](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pages.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pages.yml)

**A local-first, hardware-aware control plane for existing AI engines and models.**

Project site: **https://azmisahin-ai.github.io/local-ai-runtime/** (available after GitHub Pages is enabled and the first deploy succeeds)

This is an early C++20 alpha. It orchestrates existing model engines; it does not implement inference itself. The product name and API are still evolving.

## For users

### Downloads

Use the project site or [GitHub Releases](https://github.com/azmisahin-ai/local-ai-runtime/releases). After a version tag is published, GitHub Actions builds portable packages for Windows x64 and Linux x64 and attaches `SHA256SUMS.txt`.

The package includes the runtime, the web dashboard, an example configuration, a starter script, and the project license. **It does not include `llama.cpp`, other inference engines, CUDA, or model weights yet.** The example engine paths are placeholders and must be updated on your machine.

- **Windows 10/11 x64:** extract the ZIP, open PowerShell in the extracted folder, and run:

  ```powershell
  Set-ExecutionPolicy -Scope Process Bypass
  .\scripts\Start-LocalAIRuntime.ps1
  ```

  The script creates `config/engines.json` from the example on first start and opens the dashboard.
- **Linux x64:** extract the TAR.GZ and run `./scripts/start-runtime.sh`. The runtime stays in the foreground; press Ctrl+C to stop.
- **Offline use:** the local runtime does not require GitHub once installed. Release update checks are skipped if the service is unreachable. When the dashboard is opened, it requests public release metadata from GitHub; it does not upload prompts, model paths, or usage events. GitHub still receives ordinary network metadata such as the request IP address.

The local dashboard checks for a newer **stable** release on startup and links to release notes/downloads. This alpha does **not** install or replace binaries silently. Review the package and release notes before updating.

### Security warning

The API has no authentication. It binds to `127.0.0.1` and should not be exposed directly to a LAN or the public internet. A process running on the same computer can call the API. Do not run packages from untrusted sources.

## For developers

### Prerequisites

- CMake 3.24+
- C++20 compiler (Visual Studio Build Tools with **Desktop development with C++** on Windows; GCC/Clang on Unix)
- Git
- Python 3.10+ for the HTTP end-to-end smoke test
- vcpkg on Windows (the helper bootstraps a local copy on the first run); Boost 1.74+ headers on Unix

### Build from source

**Windows PowerShell** (from repository root):

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1
```

**Ubuntu/Debian:**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libboost-dev python3
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

Open http://127.0.0.1:8000. To configure a real engine, copy `config/engines.example.json` to `config/engines.json` and edit the executable/model paths for your machine.

## CI and releases

- [CI runs](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml): Windows/MSVC, Ubuntu/GCC, macOS/Clang. CTest uses a fake engine and does not download models or require a GPU.
- [Website deployment](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/pages.yml): each push to `main` deploys `site/` using GitHub Pages. Enable **Settings → Pages → Source → GitHub Actions** once.
- [Release workflow](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/release.yml): pushing a `v*` tag runs Windows and Linux tests, packages portable downloads, generates SHA-256 checksums, and publishes a GitHub Release.
- See [release and update process](docs/release-and-update.md) before publishing a tag.

GitHub Actions is the source for repeatable cross-platform build/test results. Local hardware/model checks are separate, and no NVIDIA/CUDA combination is claimed supported until that exact configuration is tested and recorded.

## Current feature slice

- C++20 core, CMake builds, JSON engine manifest.
- Start/stop explicitly configured child processes without shell command concatenation.
- Per-engine output/error logs and basic process status.
- Loopback-only HTTP API and same-origin dashboard.
- Non-streaming OpenAI-compatible chat proxy and `/v1/models` forwarding.
- `/version` and `/health` endpoints; dashboard checks the public release feed for stable update notices.
- Fake-engine core and HTTP end-to-end tests; Windows/Linux/macOS CI definitions.
- Public project page and tag-driven Windows/Linux release pipeline.

Not implemented yet: real-model downloads/management, automatic hardware/backend selection, CUDA/VRAM probes, a secure unattended self-updater, installers, audio/vision/embedding adapters, streaming chat, authentication, and MCP. See [roadmap](docs/roadmap.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [architecture](docs/architecture.md), [engine/model distribution strategy](docs/engine-distribution.md), [release/update process](docs/release-and-update.md), [MCP plan](docs/mcp-integration.md), and [security policy](SECURITY.md). Update `site/product.json` when user-visible features or their status change. Add regression tests for behavior changes.
