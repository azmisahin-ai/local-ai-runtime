# Real engine validation on Windows

The canonical CI workflow uses a fake OpenAI-compatible engine so that CI never downloads a model or claims support for a specific GPU. Use the repository script `scripts/Test-LocalAIRuntimeRealEngine.ps1` on a Windows 10/11 x64 machine after installing the reviewed llama.cpp component and a GGUF model. The helper is not included in the portable ZIP; run it from a source checkout and point `-RuntimeRoot` at the extracted release folder. This keeps the alpha updater’s strict ZIP file allowlist backward-compatible.

## Before running

1. Download and extract the Windows x64 release ZIP to a user-writable folder such as `Downloads\LocalAIRuntime`.
2. Start the runtime once with `scripts\Start-LocalAIRuntime.ps1`.
3. In the dashboard at `http://127.0.0.1:8000`, use **Managed Components** to install the llama.cpp engine and starter Qwen2.5 1.5B Instruct Q4_K_M model. Accept their licenses only after reviewing them. Wait for both jobs to finish.
4. Confirm that the dashboard has configured `llama-server.exe` and the GGUF file as the default chat engine. The test does not download components, edit configuration, or change the selected backend.

The first model download is about 1.12 GB and needs a network connection and additional free disk space. The runtime itself is smaller. For the first baseline, select CPU mode; GPU mode is a separate, machine-specific test.

## Run the smoke test

Open PowerShell 5.1 in the source checkout root (the folder containing `scripts\Test-LocalAIRuntimeRealEngine.ps1`):

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\Test-LocalAIRuntimeRealEngine.ps1 -RuntimeRoot "$HOME\Downloads\LocalAIRuntime"
```

Replace the sample `RuntimeRoot` path with the folder where you extracted the Windows release ZIP. The script reads the existing per-user configuration from `%LOCALAPPDATA%\LocalAIRuntime\config\engines.json`, validates that the configured default engine is `llama-server.exe` and that the model has a `GGUF` header, starts the runtime if it is not healthy, starts the configured engine if needed, waits for the runtime's readiness probe, queries `/v1/models`, and sends a small non-streaming chat request through `/v1/chat/completions`.

By default, resources the script starts are shut down at the end. A runtime or engine that was already running before the test is left running. To leave a test-started runtime/engine up for manual inspection, pass `-KeepRunning`. Timeouts can be adjusted for slower hardware:

```powershell
.\scripts\Test-LocalAIRuntimeRealEngine.ps1 -ReadyTimeoutSeconds 600 -ChatTimeoutSeconds 120
```

The report is saved in `%LOCALAPPDATA%\LocalAIRuntime\logs\real-engine-smoke-<timestamp>.txt`. It records the selected engine/model filenames, configured backend hint, readiness/chat timing and pass/fail details. It deliberately omits generated response text. Review the report and local engine log before sharing them; paths and error text can still reveal machine details.

## Interpreting results

- **PASS** means this exact machine could start the configured `llama-server.exe`, load the selected GGUF, answer the local readiness probe and produce a non-empty response through Local AI Runtime's chat API.
- **PASS does not mean CUDA is proven.** The report's backend is derived from the configured `--n-gpu-layers` setting. `auto` means GPU layer placement was requested, not that CUDA was detected or used. For a CUDA claim, record the GPU model, driver version, VRAM, model quantization, llama.cpp asset/tag and engine log showing the actual backend.
- **FAIL** preserves the report and leaves pre-existing runtime/engine processes untouched. Inspect `%LOCALAPPDATA%\LocalAIRuntime\logs\runtime.log` and the corresponding engine log before retrying.

The test is local-only. It does not upload prompts, model responses, logs, or diagnostics.
