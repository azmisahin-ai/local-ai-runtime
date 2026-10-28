# Architecture — 0.1-alpha

```text
Browser UI (same origin)
        |
        v
Loopback HTTP API (Boost.Asio / Boost.Beast)
        |                       \
        v                        v
EngineManager              Chat proxy
        |                        |
        v                        v
ManagedProcess ----------> local llama-server HTTP API
        |
        v
per-user-data/logs/<engine-id>.log
```

## Boundaries

- **Core/config:** reads and atomically persists a user-owned JSON manifest; validates engine IDs, loopback binding and upstream URLs.
- **Process manager:** starts one configured engine with a direct argument vector; monitors exit state; captures stdout/stderr to a per-engine log; refuses to replace configuration while an engine is running.
- **HTTP API:** exposes status/start/stop/setup/chat proxy routes. The listener binds only to IPv4 loopback. The llama.cpp setup endpoint accepts explicit local paths and writes them to the user configuration; it never downloads or executes setup scripts.
- **UI:** static same-origin browser page with no build-time JavaScript dependencies. At startup it checks the public GitHub release feed for a newer version; this is a notification/link, not a silent updater. The setup form configures an installed `llama-server` and local GGUF model.
- **Engine adapter:** v0.1 uses process launch plus an OpenAI-compatible local upstream. Readiness is determined by `GET /health`, falling back to `GET /v1/models` only when the upstream does not implement `/health`. Model inference remains inside the engine process.

## Important invariants

1. Do not start engine commands through a shell.
2. Never proxy an arbitrary user-provided URL; v0.1 upstreams must be localhost URLs.
3. Do not infer CUDA support from the presence of a GPU.
4. Keep model weights and machine-specific configuration out of Git.
5. Separate process-running status from health-tested status; the API reports both `status` and `ready`, and the dashboard only enables chat when the default engine is ready.
6. Do not expose the unauthenticated setup/process API beyond loopback. The setup endpoint can configure an executable path, so same-machine API access is a privileged operation.

## Version and update boundary

- `GET /version` returns the embedded version and the public repository/release-feed URLs.
- The dashboard checks releases only when the UI starts. Development/prerelease builds can see prereleases; stable builds are offered stable releases only.
- The dashboard does not download or execute update binaries. An unattended updater is blocked on signed provenance, a per-platform staging/restart mechanism, rollback, and tests for partial updates.
- The Pages site reads the version-controlled `site/product.json` feature manifest and the public GitHub Releases API. The final `publish-pages` job in `pipeline.yml` deploys `site/` only after Windows x64, Linux x64, macOS x64 and macOS arm64 package jobs pass on `main`.

## Known design debt

- The server is synchronous per connection and uses detached request threads; production hardening should add bounded concurrency and orderly shutdown.
- Windows process stop currently uses `TerminateProcess`; graceful application-specific stop is a future adapter feature.
- The chat proxy is non-streaming and currently assumes the local upstream serves `/v1/chat/completions`.
- The process manager handles one active process per configured engine; there is no multi-engine GPU scheduler.
- JSON configuration parsing still uses Boost.PropertyTree and therefore does not preserve all JSON type distinctions. A stricter schema/JSON library should be considered before a stable release.
- The setup form requires users to bring a `llama-server` binary and `.gguf` file; verified downloads, resumability and model-license metadata are not implemented yet.
- Localhost access is not authentication. This release should not be exposed beyond the same machine.
