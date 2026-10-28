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
logs/<engine-id>.log
```

## Boundaries

- **Core/config:** reads a user-owned JSON manifest; validates engine IDs, loopback binding and upstream URLs.
- **Process manager:** starts one configured engine with a direct argument vector; monitors exit state; captures stdout/stderr to a per-engine log; requests stop and uses a forced fallback on POSIX.
- **HTTP API:** exposes status/start/stop/chat proxy routes. The listener binds only to IPv4 loopback.
- **UI:** static same-origin browser page with no build-time JavaScript dependencies.
- **Engine adapter:** v0.1 uses process launch plus an OpenAI-compatible local upstream. Model inference remains inside the engine process.

## Important invariants

1. Do not start engine commands through a shell.
2. Never proxy an arbitrary user-provided URL; v0.1 upstreams must be localhost URLs.
3. Do not infer CUDA support from the presence of a GPU.
4. Keep model weights and machine-specific configuration out of Git.
5. Separate process-running status from health-tested status; this prototype currently reports process status only.

## Known design debt

- The server is synchronous per connection and uses detached request threads; production hardening should add bounded concurrency and orderly shutdown.
- Windows process stop currently uses `TerminateProcess`; graceful application-specific stop is a future adapter feature.
- The chat proxy is non-streaming and currently assumes the local upstream serves `/v1/chat/completions`.
- The process manager handles one active process per configured engine; there is no multi-engine GPU scheduler.
- JSON configuration uses Boost.PropertyTree and therefore does not preserve all JSON type distinctions. A stricter schema/JSON library should be considered before a stable release.
- Localhost access is not authentication. This release should not be exposed beyond the same machine.
