# UI/UX direction

## Product principle

Local AI Runtime is a private AI workspace for a person's own computer. The interface should describe what the user can accomplish before it describes engines, packages, CUDA variants, or filesystem paths. Advanced users must retain control, but technical setup should not be the default first-run experience.

## Primary navigation

The app uses five persistent destinations:

- **Overview** — current model/runtime readiness, a compact device summary, and recommendations with explicit evidence labels.
- **Chat** — the primary available workflow. It now streams SSE deltas, lets the user stop generation, and persists a bounded conversation list in the local WebView/browser profile. Regenerate, explicit model selection, system instructions and richer conversation management remain planned.
- **Models** — discover, compare, install, update, and remove models and their required engine/dependency packages. Show license, file size, storage needs, memory guidance, backend/platform requirements, provenance, and evidence without flooding the default card view.
- **Activity** — one view of current downloads, verification, installation, engine lifecycle, inference, updates, and recovery. The current `/v1/activity` v1 endpoint is only a live snapshot of download/update jobs plus engine state; a durable event ledger is still required.
- **Settings** — language, updates, storage, privacy, and advanced/manual configuration.

## First-run journey

1. Welcome and a brief local-first privacy explanation.
2. Detect the device locally and explain the difference between detected hardware and tested compatibility.
3. Recommend a small set of models based on usable RAM, VRAM, disk space, platform, and available backends. Clearly show estimates and confidence.
4. Let the user choose a model; resolve the compatible engine and dependencies behind the scenes.
5. Show one cancellable installation job with progress, verification, and actionable recovery.
6. Open Chat when the model server is ready and provide a simple first prompt.

The first-run flow should not ask ordinary users to locate executables, edit JSON, or choose among duplicate CUDA runtime assets. Those controls belong in Advanced settings.

## Visual and interaction rules

- Use a calm, high-contrast interface with a consistent spacing, typography, color, and component system.
- Give Chat a prominent route and useful empty state. Avoid placing the main conversation below hardware diagnostics and download internals.
- Keep the default Overview concise. Detailed GPU evidence, driver data, paths, checksums, and package metadata belong behind “Details” or in Activity/diagnostics.
- Planned workspaces are informational, not clickable. Never present planned functionality as installed or available.
- Every asynchronous action needs a visible state: queued, running, verifying, installing, ready, failed, canceled, or recovery required. Preserve user input during errors.
- Use inline progress for a single small action and Activity for cross-task tracking. Do not create a second, inconsistent progress model in each panel.
- Use confirmation for destructive actions and explain whether user data is affected.
- Keyboard navigation, focus visibility, labels, accessible status announcements, reduced-motion support, and non-color-only status indicators are required.

## Localization

English and Turkish are the initial locales. All UI-authored strings, dynamic status/error messages, accessible names, progress updates, empty states, and confirmation text must come from locale resources. Engine/catalog metadata remains language-neutral; localize its surrounding UI labels, not upstream names or license text. Language preference is local to the user profile.

## Honest capability presentation

The UI must separate `Listed`, `Package verified`, `Installed`, `Process ready`, `Task tested`, and `Profile validated`. Hardware detection, package installation, CUDA initialization, or an offload message alone is not a real GPU task test. Chat streaming and local history are implemented and covered by the fake-engine HTTP smoke; this does not prove every third-party OpenAI-compatible backend implements SSE identically. Do not advertise GPU execution or multimodal features until implemented and validated.

## Implementation sequence

1. Establish the navigation, visual shell, Overview/Chat hierarchy, and EN/TR resource loading.
2. Unify operation state and progress in the Activity/job contract.
3. Complete dynamic localization and accessibility.
4. Move reviewed engine/model/dependency definitions to a versioned checked-in authoring manifest that generates both catalogue metadata and install allowlists.
5. Finalize the common engine adapter lifecycle against `llama.cpp`.
6. Validate real CPU/GPU model profiles and add model-family pilots on the existing engine.
7. Only then add additional engine families and workspaces.
