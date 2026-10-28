# First delivery — user runtime and public project infrastructure

## Included

- C++20 runtime, JSON engine manifest, local process manager, per-engine log output.
- Loopback-only HTTP API and same-origin dashboard.
- Non-streaming OpenAI-compatible chat proxy and `/v1/models` forwarding.
- `GET /health` and `GET /version`; the dashboard checks the GitHub Releases API for a newer applicable version and links to release notes/downloads.
- Windows Winsock 2 header ordering fix (`winsock2.h` before `windows.h`, plus `WIN32_LEAN_AND_MEAN`/`NOMINMAX`).
- POSIX process launch uses `posix_spawn` actions rather than running C++/filesystem code in a post-fork child of a multithreaded server.
- CMake, Windows PowerShell build helper, Linux shell helper, fake-engine core test and HTTP end-to-end smoke test.
- CI definition for Windows/MSVC, Ubuntu/GCC and macOS/Clang.
- Pages workflow, dynamic release/download panel, feature manifest, release workflow, portable package launchers and SHA-256 manifest generation.
- Dependabot configuration, issue templates, contribution/security docs, release/update process and future MCP security plan.

## Locally verified in this work session

- Linux x86-64, GCC 14.2, CMake configure and build succeeded.
- CTest: **2/2 passed**: core/process lifecycle tests and HTTP end-to-end test using a local fake OpenAI-compatible engine.
- `local-ai-runtime --version` returned `0.1.0-dev`.
- `web/app.js` and `site/app.js` passed `node --check`.
- Product/config JSON parsed successfully.
- GitHub Actions, Dependabot and issue-template YAML files parsed successfully with a YAML parser.

## Not yet verified

- Windows/MSVC build after the Winsock header fix. The initial build log shared by the user showed C1189 due to the old Winsock header being included first; this package reorders the Windows socket headers, but only hosted Windows CI can confirm it.
- The latest hosted macOS Actions run still failed before this `posix_spawn` change. The fix has passed Linux integration tests (including ten repeated HTTP smoke runs) but needs a new hosted macOS run to verify the diagnosis. The macOS workflow now repeats the HTTP smoke test five times after the standard test run.
- Public GitHub Actions results for the current `posix_spawn` change; the updated bundle has not yet been pushed to the user's repository from this environment.
- GitHub Pages deployment and release publication; these workflows run only after the files are pushed and the Pages source is enabled.
- Clean Windows package startup, VCRuntime deployment, Linux package startup in a clean container/VM.
- Real llama.cpp + GGUF smoke test, CUDA, GPU memory, real model performance, and the user's RTX 3060 6 GB / 16 GB RAM PC.
- `audio.cpp`, ONNX, image/video generation, embeddings, and actual MCP server support.

## Update policy

Current implementation is notification + link to a GitHub release. It does not silently replace the running binary. Secure unattended self-updating is a later milestone after package signatures/provenance, staged replacement, restart handling, configuration/model preservation and rollback are tested.


## Follow-up fixes after first GitHub Actions run

- The macOS smoke failure showed a timeout while requesting the proxied fake model list. The test now holds the runtime port reservation while it chooses the upstream port (so the two selected ports cannot be identical), waits until the fake engine itself answers `/v1/models`, uses a bounded proxy request timeout, and prints process logs/ports on failure. This is a defensive fix for the leading port-collision/readiness hypotheses; the original run did not expose the chosen port numbers, so only a rerun on GitHub macOS can confirm the root cause.
- Developer launchers now detect binaries in the default CMake build output (`build/Release` for Visual Studio and `build/` for Ninja) or at the root of a release package.
- CI now starts the application through each platform's developer launcher, checks `/health`, and stops the process.
