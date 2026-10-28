# Local AI Runtime

[![CI](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml/badge.svg)](https://github.com/azmisahin-ai/local-ai-runtime/actions/workflows/ci.yml)

**A local-first, hardware-aware manager for existing AI engines and models.**

This is an early C++20 prototype. It orchestrates existing engine processes; it does not implement model inference itself. The product name is provisional.

## v0.1-alpha scope

- C++20 + CMake core.
- Loopback-only HTTP API (`127.0.0.1` by default).
- JSON engine manifest.
- Process start/stop, exit status, PID, and stdout/stderr log file per engine.
- Configurable `llama-server` executable adapter with `{model_path}` argument expansion.
- OpenAI-compatible, non-streaming chat proxy to a configured local upstream.
- Small same-origin browser dashboard.
- Core/process tests plus a Python standard-library HTTP end-to-end smoke test.
- GitHub Actions build and test jobs for Windows/MSVC and Linux/GCC.

This is **not yet a production-ready installer**. It does not download models, select a model based on hardware, schedule VRAM, guarantee CUDA compatibility, or support every OS/GPU combination. The example configuration has placeholder paths; edit it for your machine.

## Quick start — Windows 10/11

Requirements: Git for Windows, CMake 3.24+, and Visual Studio Build Tools/Visual Studio with **Desktop development with C++**. Python 3 is recommended for the HTTP smoke test; the C++ core tests still build without Python.

From the repository root, run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1
```

On the first run, this helper checks `VCPKG_ROOT` / `VCPKG_INSTALLATION_ROOT`. If neither points to an existing vcpkg installation, it downloads vcpkg into `.tools/vcpkg`, bootstraps it, and lets CMake's manifest mode install the dependencies declared in `vcpkg.json`. The first run therefore requires internet access. `.tools/` and vcpkg's installed packages are ignored by Git and are not included in the source bundle.

The script configures, builds, and runs the tests, failing immediately if any stage fails. Optional examples:

```powershell
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Clean
```

### Windows — manual CMake path

If you already manage vcpkg yourself, use its toolchain file. A plain `cmake -S . -B build` on a fresh machine will not find Boost unless Boost is otherwise installed/configured.

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_MODE=ON -DLAR_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Linux — Ubuntu/Debian

Install the system dependencies once:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libboost-dev python3
```

Then use the helper or equivalent CMake commands:

```bash
./scripts/build.sh
# or
cmake -S . -B build -DLAR_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Configure an engine

Copy the example file and edit the executable/model paths. Do not commit machine-specific config.

PowerShell:

```powershell
Copy-Item config/engines.example.json config/engines.json
notepad config/engines.json
```

Example fragment:

```json
{
  "id": "llama-cpp",
  "display_name": "llama.cpp server",
  "executable_path": "C:/AI/llama.cpp/llama-server.exe",
  "model_path": "C:/AI/models/your-model.gguf",
  "working_directory": "",
  "args": ["--host", "127.0.0.1", "--port", "8081", "--model", "{model_path}"],
  "upstream_base_url": "http://127.0.0.1:8081",
  "capabilities": ["chat"],
  "default_for_chat": true
}
```

Arguments are arrays and passed directly to the executable; there is no shell interpolation. Only `{model_path}` is expanded. Logs are written to `logs/<engine-id>.log` under the runtime's current working directory.

## Run

Run from the repository root so `web/` and `config/` resolve. For a Visual Studio multi-configuration build:

```powershell
.\build\Release\local-ai-runtime.exe
```

The default address is `http://127.0.0.1:8000`. Open it in a browser. To see CLI options, pass `--help`.

## API

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/health` | Runtime health |
| `GET` | `/v1/engines` | Configured engines and process state |
| `GET` | `/v1/models` | Proxy the selected engine's model list |
| `POST` | `/v1/engines/{id}/start` | Start a configured engine |
| `POST` | `/v1/engines/{id}/stop` | Stop a configured engine |
| `POST` | `/v1/chat/completions` | Forward a non-streaming chat request to the local default engine |

Streaming, full OpenAI API compatibility, engine readiness probes, process auto-restart, installers, audio, vision, model downloads, and hardware discovery are not implemented in this first slice.

## Tests and GitHub Actions

`ctest` runs:

- `lar_core_tests`: config validation, safe argument expansion, and process lifecycle.
- `lar_http_smoke` (when Python 3 is available): starts the runtime and a fake local OpenAI-compatible engine, then verifies dashboard, health, engine start/stop, model proxy, and chat proxy end-to-end. No real model or GPU is required.

GitHub Actions installs Boost and builds/tests on Ubuntu, and uses the hosted Windows runner's vcpkg installation to build/test with MSVC. This catches compile and general behavior regressions across both operating systems. It cannot prove that a particular NVIDIA driver, CUDA build, real model, or GPU memory configuration works; these require hardware smoke tests and must be reported separately.

## Security baseline

- Binds only to loopback in this alpha; non-loopback bind addresses are rejected.
- Rejects non-loopback Host headers and cross-origin browser requests.
- Limits request bodies to 4 MiB and upstream response bodies to 16 MiB.
- Starts children using an explicit argument vector, never `system()` or shell command concatenation.
- Does not download models or execute model-provided install scripts.
- No telemetry.

There is no authentication. Any local process able to reach the loopback API may use it. Do not expose it to a LAN or the public internet.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [docs/architecture.md](docs/architecture.md), [docs/roadmap.md](docs/roadmap.md), and [docs/first-delivery.md](docs/first-delivery.md). Add regression tests for behavioral changes and never claim hardware/model compatibility unless the exact combination has been tested and recorded.
