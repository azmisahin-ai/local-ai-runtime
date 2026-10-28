# First implementation delivery

## Included

- C++20 / CMake runtime and process-manager library.
- JSON-configured engine manifest; explicit executable and argument-array launch.
- `{model_path}` argument expansion, process state/PID/exit status, and logs under `logs/`.
- Loopback HTTP API and same-origin browser dashboard.
- `GET /health`, `GET /v1/engines`, `GET /v1/models`, engine start/stop, and non-streaming chat proxy endpoints.
- Windows/POSIX process implementations.
- Core tests plus a cross-platform Python standard-library HTTP smoke test using a fake local OpenAI-compatible engine.
- GitHub Actions jobs for Windows/MSVC and Linux/GCC.
- A Windows build helper that detects an existing vcpkg setup or bootstraps a local one before CMake configure.

## Verified for this revised bundle

- Linux x86_64, GCC 14.2, CMake 3.31, Boost 1.83.
- CMake configure succeeded and all targets compiled.
- CTest: 2/2 passed: core/process lifecycle test and HTTP end-to-end smoke test using a fake OpenAI-compatible child engine.
- HTTP smoke coverage: dashboard, health, engine listing/start/stop, model-list proxy, and chat proxy.

Windows/MSVC CI cannot be claimed as passing until this bundle is uploaded and the GitHub Actions workflow completes. This environment did not execute a Windows build.

## Not yet verified

- Windows/MSVC CI run.
- The newly added HTTP smoke test on both Linux and Windows until the revised bundle's test run completes.
- A real `llama-server` executable and real model.
- CUDA/GPU/VRAM/driver combinations, including the user's RTX 3060 6 GB machine.
- audio.cpp integration.

## Known limitations

- The API has no authentication; keep it bound to loopback.
- HTTP client handling uses detached per-request threads; bounded concurrency and orderly worker joining are future improvements.
- Process `running` means OS process state, not that a model is ready to accept inference. The HTTP smoke test polls the fake engine; production engine readiness probing is still needed.
- On Windows, stopping a process currently terminates the configured process directly; Job Object cleanup and engine-specific graceful shutdown can be improved.
- JSON manifest validation uses Boost.PropertyTree and is basic rather than schema-complete.
- The example config contains placeholder Windows paths and must be edited on the target machine.
