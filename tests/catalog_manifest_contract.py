#!/usr/bin/env python3
"""Validate the checked-in component manifest, install allowlist and API metadata."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from catalog_contract import validate_catalog_schema  # noqa: E402


def fail(message: str) -> None:
    raise SystemExit(f"catalog manifest contract failed: {message}")


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
            if entry.get("kind") != "model" or entry.get("default_model") is not True:
                fail(f"{component_id}: only an explicitly default model may use pinned-gguf")
            defaults += 1
            filename = install.get("filename", "")
            url = install.get("url", "")
            digest = install.get("sha256", "")
            size = install.get("size_bytes", 0)
            if not re.fullmatch(r"[A-Za-z0-9._-]+", filename) or filename in {".", ".."}:
                fail(f"{component_id}: unsafe model filename")
            if not url.startswith("https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/") or not url.endswith("/" + filename):
                fail(f"{component_id}: model URL is outside the reviewed immutable upstream path")
            if not re.fullmatch(r"[a-fA-F0-9]{64}", digest) or size <= 0:
                fail(f"{component_id}: model pin requires SHA-256 and exact positive size")
            if metadata.get("sha256") != digest or metadata.get("size_bytes") != size:
                fail(f"{component_id}: install pin and display metadata do not agree")
            if "command" in install or "script" in install:
                fail(f"{component_id}: component manifest may not contain executable install commands")
        if entry.get("default_model", False) and strategy != "pinned-gguf":
            fail(f"{component_id}: only pinned model entries can be the default model")

    if defaults != 1:
        fail(f"expected exactly one default model, got {defaults}")
    if variants != {"cpu", "cuda12", "cuda13"}:
        fail(f"unexpected engine release variant set: {sorted(variants)}")
    print(f"Catalog manifest contract passed: {len(components)} reviewed components, {len(ids)} unique IDs, {len(variants)} engine variants.")


if __name__ == "__main__":
    main()
