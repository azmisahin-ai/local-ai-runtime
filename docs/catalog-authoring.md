# Catalog authoring guide

The catalog is the reviewed list of components a user may install. It is not a generic downloader and it is not a plugin marketplace. The current runtime still uses a built-in install allowlist; the v1 metadata schema is the contract for catalog data and the next step toward a single reviewed manifest.

## Component kinds

- **Engine**: executable runtime or adapter (for example, `llama.cpp` CPU or CUDA 12). It declares tasks, platform/backend targets, external requirements, resource behavior, dependencies, installation policy and evidence status.
- **Model**: weight/checkpoint (for example, a pinned GGUF file). It declares format, immutable revision, exact size/hash when pinned, license, memory guidance and supported engine families. A model's format compatibility does not guarantee that it fits a device.
- **Dependency**: a verified prerequisite needed by an engine. Supporting archives such as CUDA runtime DLLs are listed under the parent engine's `provenance.dependencies`; do not show them as duplicate user-facing engines.

## Required metadata contract

The `/v1/catalog` response follows [`catalog/schemas/component-v1.schema.json`](../catalog/schemas/component-v1.schema.json). The contract covers:

- identity and versioning (`schema_version`, stable ID, kind/type, family, version policy);
- tasks/workspaces/modalities and input/output formats;
- supported OS/architecture pairs, backend, driver requirements and known limitations;
- download size policy and RAM/VRAM estimates with an explicit confidence value;
- canonical source, revision policy, license, digest policy and dependencies;
- package verification, real task test and hardware profile evidence;
- per-user install location and update policy.

If a field is unknown, use `null`, an empty list when appropriate, or an explicit “not verified” status. Do not guess a minimum VRAM requirement or claim a model/backend is supported from a detected GPU alone. Estimates labelled `heuristic` must stay visibly estimates.

## Adding a new component safely

1. Identify one concrete task/workspace and the maintained upstream project. Review the exact component license and redistribution terms.
2. Identify the immutable release/revision and the exact OS/architecture/backend assets. Prefer a published digest or pin a known digest; never disable TLS/hash checks to make an asset work.
3. Define all companion dependencies under the selected engine. Verify each asset, enforce download and expanded-size limits, reject unsafe archive paths, then activate the complete set atomically.
4. Add a record matching component-v1, plus a resolver that accepts only the reviewed ID and known official asset naming. Arbitrary URLs and repository-supplied install scripts are not allowed.
5. Add regression tests for asset selection ambiguity, unsupported targets, license acknowledgement, checksum mismatch, cancellation, extraction, activation rollback and uninstall/update behavior.
6. Add a real task smoke test for each claimed target profile. Record OS, CPU/GPU, driver, RAM/VRAM, model revision/quantization, context, output success and memory observations. Hosted fake-engine CI is not real-task evidence.
7. Keep the component state honest: `listed`, `package verified`, `installed`, `process ready`, `task tested`, and `profile validated` are separate milestones.

## What v1 does not mean

A valid schema record does not make a component installable by itself. The current catalog is still a built-in reviewed allowlist with CPU/CUDA `llama.cpp` options and one pinned Qwen GGUF model. The versioned v1 metadata contract is validated against the actual API response in the HTTP smoke test; the install resolver remains explicit code until the planned manifest migration is complete.
