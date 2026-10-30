# Catalog authoring guide

The catalog is the reviewed list of components a user may install. It is not a generic downloader or a plugin marketplace. `catalog/manifest-v1.json` is the checked-in, versioned source of truth for both public component metadata and the runtime install allowlist.

At CMake configure time, the manifest is embedded into the runtime binary. The runtime never downloads a manifest and never reads install policy from a user-writable data directory. A catalog entry is not installable merely because it has a valid display record: its install strategy, immutable source pin, targets and metadata identity must all pass the runtime's compiled validation.

## Manifest structure

- `schema_version` describes the manifest contract; `manifest_version` versions reviewed catalog content.
- Each component has a stable `id`, `kind`, `installable` flag, `default_model` marker, an `install` policy, and `metadata_json`.
- `metadata_json` is the compact canonical `component-v1` JSON object stored as a string so the compiled runtime can preserve JSON scalar types exactly when it adds runtime-derived `installed` and `installed_path` properties.
- `install.targets` is the install allowlist for OS/architecture pairs. It must exactly match `metadata_json.compatibility.targets`; display claims and install permissions cannot drift apart.
- `metadata_json.provenance.dependencies` is an explicit, reviewed dependency plan. In the current v1 starter catalogue it describes the matching CUDA runtime archive for CUDA 12/13 llama.cpp packages. That archive is resolved from the same official release, verified and installed as part of the parent engine job; it is not an independent user-selectable component.
- The only accepted installer strategies in v1 are `llama-release` and `pinned-gguf`. Unknown strategy names fail closed.

The contracts are:

- [`manifest-v1.schema.json`](../catalog/schemas/manifest-v1.schema.json): authoring manifest shape and allowed installer fields.
- [`component-v1.schema.json`](../catalog/schemas/component-v1.schema.json): public `/v1/catalog` metadata returned by the runtime.

## Supported v1 installer strategies

### `llama-release`

Used only for reviewed `llama.cpp` engine variants (`cpu`, `cuda12`, `cuda13`). The manifest chooses the stable component ID, variant, and supported targets. Runtime code still owns the constrained official GitHub Releases API resolver and its exact asset-name selection rules. It requires official SHA-256 metadata, rejects ambiguous matches, pairs CUDA server/runtime archives from the same release, verifies both packages, and safely extracts them. A manifest cannot provide an arbitrary download URL, executable command, or install script.

### `pinned-gguf`

Used for the default Qwen GGUF starter model. The install record pins a reviewed Hugging Face model repository, immutable 40-character revision, exact filename, SHA-256 and byte size. Runtime validation requires the exact reviewed upstream URL namespace, a path-safe filename, a 64-character digest, a positive bounded size, and matching digest/size in the public metadata. A manifest cannot provide an executable command or install script.

## Installation queue and dependency behavior

All component and runtime-update downloads enter one persistent FIFO installation queue. The runtime accepts up to 16 outstanding installation jobs (including the active job) and uses one installation worker, so only one download/extract/configure pipeline runs at a time. Repeated requests for the same queued or active component are idempotent and return the existing job instead of creating duplicates. The queue is bounded; a full queue returns HTTP `409` and remains visible in Activity. A queued item can be canceled before it starts; an active transfer receives a cancellation request and is finalized at a safe cancellation point.

CUDA 12 and CUDA 13 engine entries declare their matching runtime archive in `provenance.dependencies`. The runtime resolves the server and companion archive from the same official release, verifies both SHA-256 values, and stages them under one parent job. The Activity job exposes `queue_position` and `dependency_plan`; it does not create a separate synthetic job for an internal DLL/archive dependency. CPU llama.cpp and the pinned GGUF starter have no additional installer dependency.

Queued entries are persisted and revalidated against the embedded manifest, platform allowlist and recorded license acceptance at startup. Safe jobs that had not started resume in FIFO order. A job whose transfer was already active at shutdown is recorded as interrupted and is not replayed automatically; the user must explicitly retry it. This prevents an ambiguous partial download or install from silently running twice.

## Adding or changing a component safely

1. Identify one concrete task/workspace and a maintained upstream project. Review the exact component license and redistribution terms.
2. Select one of the explicitly supported installer strategies. Do not add an arbitrary URL, command, script, repository-controlled installer, or remote manifest source.
3. Pin immutable upstream revision, exact filename, size and SHA-256 for `pinned-gguf`; for `llama-release`, update the constrained official asset selector and regression tests only after the exact upstream asset naming and companion runtime have been reviewed.
4. Update `catalog/manifest-v1.json`. Keep `install.targets` exactly equal to the component metadata's `compatibility.targets`; validate the public metadata against component-v1.
5. Run `tests/catalog_manifest_contract.py`, the C++ core tests, and HTTP smoke. These cover unique IDs, default-model uniqueness, target parity, installer allowlist, immutable model pin, actual `/v1/catalog` schema and rejection of unknown download IDs.
6. Add regression tests for unsupported targets, license acknowledgement, checksum mismatch, cancellation, extraction, activation rollback and uninstall/update behavior.
7. Add a real task smoke test for each claimed target profile. Record OS, CPU/GPU, driver, RAM/VRAM, model revision/quantization, context, output success and memory observations. Hosted fake-engine CI is not real-task evidence.
8. Keep component state honest: `listed`, `package verified`, `installed`, `process ready`, `task tested`, and `profile validated` are separate milestones.

## Security invariants

- The manifest is embedded in the shipped executable at build time; local users cannot replace its install allowlist by editing a JSON file.
- `installable` must be explicit and the installer strategy must be compiled into v1. Unknown IDs, strategies, targets, URLs, filenames, or digests are rejected.
- Metadata target declarations must equal the install allowlist targets.
- A valid metadata schema record does not make a component installable by itself.
- No model or engine is automatically installed or updated from a catalog refresh.
- “Detected” does not mean “supported”; “package installed” does not mean “real inference tested.”

The v1 manifest currently contains the reviewed `llama.cpp` CPU/CUDA 12/CUDA 13 entries and one pinned Qwen2.5 1.5B GGUF model. It is the single source for those components' display metadata and installer allowlist, not yet a catalog of every relevant C/C++ engine or model.
