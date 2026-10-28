# First delivery — user runtime and public project infrastructure

## Included

- C++20 runtime, JSON engine manifest, local process manager, per-engine log output.
- Loopback-only HTTP API and same-origin dashboard.
- Non-streaming OpenAI-compatible chat proxy and `/v1/models` forwarding.
- Dashboard setup form to configure an existing `llama-server` executable and local GGUF model; configuration is atomically saved and applied live only when managed engines are stopped.
- Built-in managed catalog for the official llama.cpp CPU/CUDA release archive and a pinned Qwen2.5 1.5B Q4_K_M GGUF model; HTTPS-only transfers, explicit license acceptance, content-size caps, SHA-256 checks, GGUF magic validation, safe archive extraction, per-user installation paths, cancellation and job progress.
- Engine readiness probe using local `/health` (with `/v1/models` fallback for older servers); the dashboard enables chat only after readiness.
- `GET /health` and `GET /version`; the dashboard checks the GitHub Releases API for a newer applicable version and links to release notes/downloads.
- Windows Winsock 2 header ordering fix (`winsock2.h` before `windows.h`, plus `WIN32_LEAN_AND_MEAN`/`NOMINMAX`).
- POSIX process launch uses `posix_spawn` actions rather than running C++/filesystem code in a post-fork child of a multithreaded server.
- CMake, Windows PowerShell build helper, Linux shell helper, fake-engine core test and HTTP end-to-end smoke test. The HTTP fixture is a native C++/Boost.Beast executable so CI exercises the actual native child-process path on each OS.
- CI definition for Windows/MSVC, Ubuntu/GCC and macOS/Clang.
- Pages workflow, dynamic release/download panel, feature manifest, release workflow, portable package launchers and SHA-256 manifest generation.
- Dependabot configuration, issue templates, contribution/security docs, release/update process and future MCP security plan.

## Locally verified in this work session

- Linux x86-64, GCC 14.2, CMake configure and build succeeded.
- CTest: **2/2 passed**: core/process lifecycle tests and HTTP end-to-end test using a local fake OpenAI-compatible engine.
- `local-ai-runtime --version` returned `0.1.0-dev`.
- `web/app.js` and `site/app.js` passed `node --check`.
- Product/config JSON parsed successfully.
- After adding the managed installer, local Linux build/CTest passed 2/2; HTTP smoke tests cover catalog metadata, pinned model size/hash/license, missing consent, unknown IDs and no-download side effects for rejected requests.
- GitHub Actions, Dependabot and issue-template YAML files parsed successfully with a YAML parser.

## Not yet verified

- Windows/MSVC build after the Winsock header fix. The initial build log shared by the user showed C1189 due to the old Winsock header being included first; this package reorders the Windows socket headers, but only hosted Windows CI can confirm it.
- Previous hosted macOS Actions runs failed to bring up a Python-based fake child process even though the runtime reported it as running. The HTTP fixture has now been changed to a native C++ executable so the CI test isolates the platform process manager and HTTP proxy from Python launcher/environment behavior. Linux passes locally (including ten repeated HTTP smoke runs); hosted macOS CI must confirm the change. The macOS workflow repeats the HTTP smoke test five times after the standard test run.
- Latest public GitHub Actions results should be verified after the user's next push; hosted Windows/Linux/macOS CI passing does not certify a real model or GPU backend.
- GitHub Pages deployment and release publication; these workflows run only after the files are pushed and the Pages source is enabled.
- Clean Windows package startup, VCRuntime deployment, Linux package startup in a clean container/VM.
- Actual network download of the official engine/model, binary execution after installation, and real llama.cpp + GGUF inference; CI intentionally avoids large downloads. CUDA, GPU memory, real model performance, and the user's RTX 3060 6 GB / 16 GB RAM PC remain separate device tests. The HTTP test uses a fake engine and does not claim real inference support.
- `audio.cpp`, ONNX, image/video generation, embeddings, and actual MCP server support.

## Update policy

Current implementation is notification + link to a GitHub release. It does not silently replace the running binary. Secure unattended self-updating is a later milestone after package signatures/provenance, staged replacement, restart handling, configuration/model preservation and rollback are tested.


## Follow-up fixes after first GitHub Actions run

- The macOS smoke failure showed that a Python child process remained reported as running but did not answer its local API. The HTTP fixture now uses a native C++/Boost.Beast fake engine, waits for its readiness marker and API, and prints process state/logs on failure. This removes Python child-launch behavior from the native process manager test; hosted macOS CI must confirm the fix.
- Developer launchers now detect binaries in the default CMake build output (`build/Release` for Visual Studio and `build/` for Ninja) or at the root of a release package.
- CI now starts the application through each platform's developer launcher, checks `/health`, and stops the process.

## Dashboard llama.cpp setup slice

- The dashboard accepts an absolute path to an installed `llama-server` (`llama-server.exe` on Windows) and a non-empty `.gguf` model file. It validates the selected files, chooses CPU (`--n-gpu-layers 0`) or GPU (`--n-gpu-layers auto`), context size and a local engine port.
- Configuration is written to the current user config file using a temp file and backup/replace sequence. Changes are rejected while any managed engine is running.
- The setup endpoint is loopback-only by policy and shares the existing local API security model; do not expose the unauthenticated service to LAN or public networks.
- Readiness is based on an actual local HTTP probe, not only the process PID. This has been exercised against the native fake HTTP engine in Linux CI; a real `llama-server` + GGUF and GPU test remain separate gates.
