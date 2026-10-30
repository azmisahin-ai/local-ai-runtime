#!/usr/bin/env python3
"""Validate the checked-in component manifest, install allowlist and API metadata."""
from __future__ import annotations

import copy
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from catalog_contract import validate_catalog_schema  # noqa: E402


def fail(message: str) -> None:
    raise SystemExit(f"catalog manifest contract failed: {message}")




def validate_pinned_gguf_contract(component_id: str, entry: dict, metadata: dict) -> None:
    install = entry.get("install", {})
    if entry.get("kind") != "model":
        fail(f"{component_id}: pinned-gguf must be a model component")
    filename = install.get("filename", "")
    url = install.get("url", "")
    digest = install.get("sha256", "")
    size = install.get("size_bytes", 0)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,254}", filename) or filename in {".", ".."}:
        fail(f"{component_id}: unsafe model filename")
    match = re.fullmatch(
        r"https://huggingface\.co/([A-Za-z0-9][A-Za-z0-9._-]{0,95})/([A-Za-z0-9][A-Za-z0-9._-]{0,95})/resolve/([a-fA-F0-9]{40})/([A-Za-z0-9][A-Za-z0-9._-]{0,254})",
        url,
    )
    if not match:
        fail(f"{component_id}: model URL must be a single-file immutable Hugging Face revision URL")
    owner, repository, revision, url_filename = match.groups()
    expected_source = f"https://huggingface.co/{owner}/{repository}"
    provenance = metadata.get("provenance", {})
    if url_filename != filename:
        fail(f"{component_id}: URL filename differs from install filename")
    if metadata.get("source_url") != expected_source or provenance.get("source_url") != expected_source:
        fail(f"{component_id}: metadata source repository does not match the pinned URL")
    if metadata.get("version") != revision or provenance.get("revision") != revision:
        fail(f"{component_id}: metadata revision does not match immutable URL revision")
    if provenance.get("revision_policy") != "pinned-revision":
        fail(f"{component_id}: pinned-gguf requires pinned-revision provenance")
    if provenance.get("digest_policy") != "pinned-sha256-and-exact-size":
        fail(f"{component_id}: pinned-gguf requires exact SHA-256 and byte-size provenance")
    if not re.fullmatch(r"[a-fA-F0-9]{64}", digest) or not isinstance(size, int) or size <= 0 or size > 16 * 1024 * 1024 * 1024:
        fail(f"{component_id}: model pin requires SHA-256 and bounded exact positive size")
    if metadata.get("sha256") != digest or metadata.get("size_bytes") != size:
        fail(f"{component_id}: install pin and display metadata do not agree")
    if metadata.get("model_profile", {}).get("format") != "GGUF":
        fail(f"{component_id}: pinned-gguf must declare GGUF model format")
    if "command" in install or "script" in install:
        fail(f"{component_id}: component manifest may not contain executable install commands")


def main() -> None:
    manifest_path = ROOT / "catalog" / "manifest-v1.json"
    schema_path = ROOT / "catalog" / "schemas" / "component-v1.schema.json"
    manifest_schema_path = ROOT / "catalog" / "schemas" / "manifest-v1.schema.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    manifest_schema = json.loads(manifest_schema_path.read_text(encoding="utf-8"))
    if manifest_schema.get("$id", "").endswith("/manifest-v1.schema.json") is False:
        fail("manifest-v1 schema identifier is missing")
    if manifest_schema.get("properties", {}).get("components", {}).get("type") != "array":
        fail("manifest-v1 schema must describe a component array")

    if manifest.get("schema_version") != 1 or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", manifest.get("manifest_version", "")):
        fail("unsupported schema or invalid manifest version")
    components = manifest.get("components")
    if not isinstance(components, list) or not components:
        fail("components must be a non-empty array")

    ids: set[str] = set()
    variants: set[str] = set()
    defaults = 0
    allowed_strategies = {"llama-release", "pinned-gguf"}
    for entry in components:
        component_id = entry.get("id", "")
        if not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,127}", component_id):
            fail(f"invalid component ID {component_id!r}")
        if component_id in ids:
            fail(f"duplicate component ID {component_id!r}")
        ids.add(component_id)

        allowed_component_keys = {"id", "kind", "installable", "default_model", "install", "metadata_json"}
        if set(entry) != allowed_component_keys:
            fail(f"{component_id}: unexpected component manifest properties {sorted(set(entry) - allowed_component_keys)}")
        install = entry.get("install", {})
        allowed_install_keys = {"strategy", "variant", "filename", "url", "sha256", "size_bytes", "targets"}
        if not set(install).issubset(allowed_install_keys):
            fail(f"{component_id}: install policy contains unknown properties")
        strategy = install.get("strategy")
        if strategy not in allowed_strategies:
            fail(f"{component_id}: unsupported installer strategy {strategy!r}")
        if entry.get("installable") is not True:
            fail(f"{component_id}: shipped component must be explicitly installable")
        targets = install.get("targets", [])
        if not targets:
            fail(f"{component_id}: no install targets")
        normalized_targets = set()
        for target in targets:
            key = (target.get("platform"), target.get("architecture"))
            if key[0] not in {"windows", "linux", "macos"} or key[1] not in {"x64", "arm64"}:
                fail(f"{component_id}: unsupported target {key!r}")
            if key in normalized_targets:
                fail(f"{component_id}: duplicate target {key!r}")
            normalized_targets.add(key)

        metadata_text = entry.get("metadata_json", "")
        if not isinstance(metadata_text, str) or not metadata_text:
            fail(f"{component_id}: metadata_json must be a JSON object string")
        metadata = json.loads(metadata_text)
        if metadata.get("id") != component_id or metadata.get("type") != entry.get("kind"):
            fail(f"{component_id}: metadata identity does not match install allowlist")
        model_profile = metadata.get("model_profile")
        if entry.get("kind") == "model":
            if not isinstance(model_profile, dict):
                fail(f"{component_id}: model entries require a structured model_profile")
            required_profile_keys = {"task_group", "architecture", "parameter_count", "format", "quantization"}
            if set(model_profile) != required_profile_keys:
                fail(f"{component_id}: model_profile must contain exactly {sorted(required_profile_keys)}")
            if model_profile.get("task_group") not in {"chat", "vision", "embeddings", "speech", "image-generation", "video", "audio"}:
                fail(f"{component_id}: unsupported model task_group")
            if model_profile.get("format") not in {"GGUF", "ONNX", "SafeTensors", "Other"}:
                fail(f"{component_id}: unsupported model format")
            for key in ("architecture", "parameter_count", "quantization"):
                if not isinstance(model_profile.get(key), str) or not model_profile[key].strip():
                    fail(f"{component_id}: model_profile.{key} must be a non-empty string")
        elif model_profile is not None:
            fail(f"{component_id}: engine entries cannot declare model_profile")
        # installed state is runtime-derived and appended by GET /v1/catalog.
        api_metadata = dict(metadata)
        api_metadata["installed"] = False
        api_metadata["installed_path"] = ""
        errors = validate_catalog_schema(
            {"schema_version": 1, "data": [api_metadata]}, schema
        )
        if errors:
            fail(f"{component_id}: component-v1 metadata is invalid: {'; '.join(errors[:8])}")

        metadata_targets = {
            (target.get("platform"), target.get("architecture"))
            for target in metadata.get("compatibility", {}).get("targets", [])
        }
        if metadata_targets != normalized_targets:
            fail(f"{component_id}: install targets must equal the public compatibility target list")
        if strategy == "llama-release":
            variant = install.get("variant")
            target_allowed = lambda target: (
                (target[0] == "windows" and target[1] == "x64") or
                (target[0] == "linux" and target[1] == "x64") or
                (variant == "cpu" and target[0] == "macos" and target[1] in {"x64", "arm64"})
            )
            if not all(target_allowed(target) for target in normalized_targets):
                fail(f"{component_id}: target matrix is not supported by the reviewed llama.cpp asset resolver")

        if strategy == "llama-release":
            variant = install.get("variant")
            if entry.get("kind") != "engine" or variant not in {"cpu", "cuda12", "cuda13"}:
                fail(f"{component_id}: invalid llama-release engine definition")
            if variant in variants:
                fail(f"{component_id}: duplicate engine variant {variant}")
            variants.add(variant)
            if metadata.get("family") != "llama.cpp":
                fail(f"{component_id}: engine family must be llama.cpp")
            if "url" in install or "command" in install or "script" in install:
                fail(f"{component_id}: release resolver must not accept arbitrary URL or scripts")
        elif strategy == "pinned-gguf":
            validate_pinned_gguf_contract(component_id, entry, metadata)
        if entry.get("default_model", False):
            if entry.get("kind") != "model" or strategy != "pinned-gguf":
                fail(f"{component_id}: only a pinned GGUF model can be the default model")
            defaults += 1

    if defaults != 1:
        fail(f"expected exactly one default model, got {defaults}")
    if variants != {"cpu", "cuda12", "cuda13"}:
        fail(f"unexpected engine release variant set: {sorted(variants)}")

    # Prove that a second, non-default model from another reviewed HF repository
    # is accepted without changing the single default-model invariant.
    starter_entry = copy.deepcopy(next(entry for entry in components if entry.get("install", {}).get("strategy") == "pinned-gguf"))
    synthetic_metadata = json.loads(starter_entry["metadata_json"])
    synthetic_id = "exampleorg-example-gguf-q4km"
    synthetic_revision = "0123456789abcdef0123456789abcdef01234567"
    synthetic_filename = "example-model-q4_k_m.gguf"
    synthetic_source = "https://huggingface.co/ExampleOrg/Example-GGUF"
    synthetic_entry = {
        **starter_entry,
        "id": synthetic_id,
        "default_model": False,
        "install": {
            **starter_entry["install"],
            "filename": synthetic_filename,
            "url": f"{synthetic_source}/resolve/{synthetic_revision}/{synthetic_filename}",
            "sha256": "a" * 64,
            "size_bytes": 123456789,
        },
    }
    synthetic_metadata.update({
        "id": synthetic_id,
        "version": synthetic_revision,
        "source_url": synthetic_source,
        "sha256": "a" * 64,
        "size_bytes": 123456789,
    })
    synthetic_metadata["provenance"].update({
        "source_url": synthetic_source,
        "revision": synthetic_revision,
    })
    validate_pinned_gguf_contract(synthetic_id, synthetic_entry, synthetic_metadata)

    def expect_pin_rejected(label: str, mutate) -> None:
        candidate_entry = copy.deepcopy(synthetic_entry)
        candidate_metadata = copy.deepcopy(synthetic_metadata)
        mutate(candidate_entry, candidate_metadata)
        try:
            validate_pinned_gguf_contract(synthetic_id, candidate_entry, candidate_metadata)
        except SystemExit:
            return
        fail(f"synthetic pin accepted invalid {label}")

    expect_pin_rejected(
        "lookalike host",
        lambda candidate, metadata: candidate["install"].update(
            url=candidate["install"]["url"].replace("huggingface.co", "huggingface.co.evil.invalid", 1)
        ),
    )
    expect_pin_rejected(
        "query string",
        lambda candidate, metadata: candidate["install"].update(url=candidate["install"]["url"] + "?download=true"),
    )
    expect_pin_rejected(
        "source repository mismatch",
        lambda candidate, metadata: metadata.update(source_url="https://huggingface.co/OtherOrg/Example-GGUF"),
    )
    expect_pin_rejected(
        "mutable revision",
        lambda candidate, metadata: candidate["install"].update(url=candidate["install"]["url"].replace(synthetic_revision, "main", 1)),
    )
    print(f"Catalog manifest contract passed: {len(components)} reviewed components, {len(ids)} unique IDs, {len(variants)} engine variants; generic non-default GGUF fixture and invalid-pin rejections passed.")


if __name__ == "__main__":
    main()
