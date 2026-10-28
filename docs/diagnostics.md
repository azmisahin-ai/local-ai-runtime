# Diagnostics and troubleshooting

Local AI Runtime keeps troubleshooting information on the device. It does not automatically upload diagnostic events, engine logs, prompts or model responses.

## From the dashboard

Use **Refresh job status** to request the latest download-job state. **Refresh catalog** only reloads the component catalogue; it does not resume or repair an installation job. Download status is also polled automatically while the dashboard is open.

Use **Export diagnostics** to save `local-ai-runtime-diagnostics.txt`. The report contains the runtime version and platform, recent runtime events, current download job states/errors/progress, and current engine process/readiness state. The report sanitizes the runtime data-directory and home-directory paths where they appear, but you should still review it before sharing. For the dashboard chat test, the log may record request start/success/failure, elapsed time, input/response character counts, HTTP status, finish reason, and token counts if supplied by the engine. It never records the prompt text or generated response text. These metrics help distinguish a transport failure from a completed but poor-quality answer; they cannot by themselves judge answer correctness.

The **Include engine process logs** option is off by default. When selected, the report also includes the recent tail of each configured engine's log. Upstream engines may write prompt fragments, model paths or other implementation-specific details to their own logs, so inspect this optional section before sharing.

You can attach the report to a GitHub issue, send it to a maintainer through another agreed support channel, or paste its relevant sections into a troubleshooting conversation. You do not have to create an issue just to receive help. Without the exported report or copied log text, maintainers cannot inspect data that stays on your computer.

## Local log locations

- Windows: `%LOCALAPPDATA%\LocalAIRuntime\logs\runtime.log` and `%LOCALAPPDATA%\LocalAIRuntime\logs\<engine-id>.log`.
- Linux: `$XDG_DATA_HOME/local-ai-runtime/logs/` when `XDG_DATA_HOME` is set, otherwise `~/.local/share/local-ai-runtime/logs/`.
- macOS developer runs: `~/Library/Application Support/LocalAIRuntime/logs/` unless `XDG_DATA_HOME` is set.

`runtime.log` records lifecycle events such as startup attempts, successful binding/listening, startup failures (including port conflicts), downloads, model size/hash verification, file finalization, completion and failures. It rotates when the file grows beyond roughly 2 MiB. Engine output is stored separately in `<engine-id>.log` files under the same user data `logs` directory; each engine log rotates to a single `.1` backup when it exceeds roughly 5 MiB (when the file is not locked).

## Dashboard stays loading or port is already in use on Windows

Closing the browser tab does not stop the runtime process. Run `scripts\Start-LocalAIRuntime.ps1` again to reuse a healthy instance from the same application folder. The launcher reads the configured `server.port`, checks `/health` without the legacy Internet Explorer parsing prompt, and opens the existing dashboard if that runtime is healthy. If a different extracted copy is running, it asks before switching to the copy you launched. The launcher first requests graceful shutdown, allowing managed engine processes to stop; only if the process fails to respond does it offer the confirmed runtime process-tree shutdown fallback. It does not kill unrelated programs. If another program owns the port, the launcher reports its PID/name/path instead of trying to terminate it.

The runtime now writes `runtime.starting` before attempting to bind and records `runtime.startup_failed` when startup fails. That is important because the dashboard diagnostic export is unavailable while the dashboard itself is not responding. In that case, copy the last 30–80 lines of `%LOCALAPPDATA%\LocalAIRuntime\logs\runtime.log` into a support message after reviewing the paths and error details. Do not delete the extracted application folder as a way to stop the runtime: a running process can outlive the browser window, and an open process handle may keep program files in use. To stop a visible runtime console normally, press Ctrl+C in that console. The local-only `POST /v1/runtime/shutdown` endpoint acknowledges the request before exiting and is used for this graceful launcher flow.

## Recovering a model installation that failed at 100%

If the job says the model was completely downloaded and SHA-256 verified but Windows could not move the file into place, close applications that may have opened that GGUF file and choose **Download & install** again after confirming the license. The runtime keeps a fully verified staging copy and should retry the finalization step without downloading the model again. The catalog refresh button alone does not retry the job.

If the job reports a size, SHA-256 or GGUF-header verification failure, the unverified staging file is discarded and the error explains that a clean download is needed.

## Privacy

The event logger does not record chat prompts or model responses. Export is user-initiated and the report is not sent automatically. Runtime-generated reports redact known user-data/home paths, but no text redaction can be perfect; inspect the report and remove private paths, model names, or sensitive information before posting it publicly. Never attach model weights, API keys, credentials, or private datasets.
