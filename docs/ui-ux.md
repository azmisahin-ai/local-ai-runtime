# UI/UX direction

## Product principle

Local AI Runtime is a private AI workspace for a person's own computer. The interface should describe what the user can accomplish before it describes engines, packages, CUDA variants, or filesystem paths. Advanced users must retain control, but technical setup should not be the default first-run experience.

## Primary navigation

The app uses five persistent destinations:

- **Overview** — current model/runtime readiness, a compact device summary, and recommendations with explicit evidence labels.
- **Chat** — the primary available workflow. It now streams SSE deltas, lets the user stop generation, and persists a bounded conversation list in the local WebView/browser profile. Regenerate, explicit model selection, system instructions and richer conversation management remain planned.
- **Models** — discover, compare, install, update, and remove models and their required engine/dependency packages. Show license, file size, storage needs, memory guidance, backend/platform requirements, provenance, and evidence without flooding the default card view.
- **Activity** — one view of persistent download/setup jobs and engine start/stop/inference operations, with current engine readiness. `GET /v1/activity` is a point-in-time snapshot and `GET /v1/jobs` is the unified durable list; the separate append-only event ledger remains future work. Active streamed inference can be canceled, while retries stay restricted to reviewed component/update jobs.
- **Settings** — language, updates, storage, privacy, and advanced/manual configuration.

## First-run journey

1. Welcome and a brief local-first privacy explanation.
2. Detect the device locally and explain the difference between detected hardware and tested compatibility.
3. Recommend a small set of models based on usable RAM, VRAM, disk space, platform, and available backends. Clearly show estimates and confidence.
4. Let the user choose a model; resolve the compatible engine and dependencies behind the scenes.
5. Show one cancellable installation job with progress, verification, and actionable recovery.
6. Open Chat when the model server is ready and provide a simple first prompt.

The current implementation exposes a dismissible three-step local-chat guide (choose and apply a model/engine profile, start the engine, send the first chat message). Hardware information remains available on Overview but is not a blocking setup step. Only the next incomplete action is enabled, so the guide does not invite users to jump ahead. It can be reopened from Settings. Chat itself shows only the one next action required: choose a profile or open Activity. A ready Chat engine offers a direct Open Chat action from Activity. The flow should not ask ordinary users to locate executables, edit JSON, or choose among duplicate CUDA runtime assets. Those controls belong in Advanced settings.

## Visual and interaction rules

- Use a calm, high-contrast interface with a consistent spacing, typography, color, and component system.
- Give Chat a prominent route and useful empty state. Avoid placing the main conversation below hardware diagnostics and download internals.
- Keep the default Overview concise. Detailed GPU evidence, driver data, paths, checksums, and package metadata belong behind “Details” or in Activity/diagnostics.
- Planned workspaces are informational, not clickable. Never present planned functionality as installed or available.
- Every asynchronous action needs a visible state: queued, running, verifying, installing, ready, failed, canceled, or recovery required. Preserve user input during errors.
- Use inline progress for a single small action and Activity for cross-task tracking. Do not create a second, inconsistent progress model in each panel.
- Use confirmation for destructive actions and explain whether user data is affected.
- Keyboard navigation includes a skip link to the main landmark, visible `:focus-visible` treatment, focusable page/section headings for guided navigation, translated accessible names and progress announcements, and reduced-motion handling. Live updates use status regions; state is conveyed by text as well as color. Native screen-reader testing on Windows/macOS/Linux remains a manual acceptance task.

## Localization

English and Turkish are the initial locales. UI-authored strings, dynamic job/engine states, update guidance/progress, accessible names, first-run statuses, empty states and confirmation text come from English/Turkish locale resources. CI checks resource-key parity, matching interpolation placeholders and referenced HTML/JS keys. Server/API error detail, upstream component names/capabilities and license text are retained as source metadata, with a localized UI label around errors where appropriate. Engine/catalog metadata remains language-neutral; localize its surrounding UI labels, not upstream names or license text. Language preference is local to the user profile.

## Honest capability presentation

The UI must separate `Listed`, `Package verified`, `Installed`, `Process ready`, `Task tested`, and `Profile validated`. Hardware detection, package installation, CUDA initialization, or an offload message alone is not a real GPU task test. Chat streaming and local history are implemented and covered by the fake-engine HTTP smoke; this does not prove every third-party OpenAI-compatible backend implements SSE identically. Do not advertise GPU execution or multimodal features until implemented and validated.

## Implementation sequence

1. Keep the five persistent destinations stable and establish the Overview/Chat hierarchy and EN/TR resource loading.
2. Unify operation state and progress in the Activity/job contract.
3. Keep first-run setup task-first: profile → ready engine → first message; show one next action at a time and provide direct cross-navigation only when the next task is ready.
4. Complete dynamic localization and accessibility; verify assistive technology manually.
4. Move reviewed engine/model/dependency definitions to a versioned checked-in authoring manifest that generates both catalogue metadata and install allowlists.
5. Finalize the common engine adapter lifecycle against `llama.cpp`.
6. Validate real CPU/GPU model profiles and add model-family pilots on the existing engine.
7. Only then add additional engine families and workspaces.
