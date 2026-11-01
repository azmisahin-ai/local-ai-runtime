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
        r"https://huggingface\.co/([A-Za-z0-9][A-Za-z0-9._-]{0,95})/([A-Za-z0-9][A-Za-z0-9._-]{0,95})/resolve/([a-fA-F0-9]{40})/(.+)",
        url,
    )
    if not match:
        fail(f"{component_id}: model URL must be a single-file immutable Hugging Face revision URL")
    owner, repository, revision, url_path = match.groups()
    url_segments = url_path.split("/")
    if any(not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,254}", segment) or segment in {".", ".."} for segment in url_segments):
        fail(f"{component_id}: model URL contains an unsafe or encoded path segment")
    url_filename = url_segments[-1]
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


def validate_audio_cpp_contract(component_id: str, entry: dict, metadata: dict) -> None:
    install = entry.get("install", {})
    variant = install.get("variant")
    if entry.get("kind") != "engine" or entry.get("default_model"):
        fail(f"{component_id}: audio-cpp-release-set must be a non-default engine")
    if variant not in {"cpu", "cuda13.3"}:
        fail(f"{component_id}: unsupported audio.cpp Windows backend")
    targets = install.get("targets", [])
    if targets != [{"platform": "windows", "architecture": "x64"}]:
        fail(f"{component_id}: audio.cpp pilot is Windows x64 only")
    pins = {
        ("cpu", "server"): (
            "audio-v0.9.3-bin-windows-x64-cpu.zip",
            "3ac66f46395b4e92e4f3bb68b200f0032310bd9922f12d97f10ccdadb02dee4b",
            30547092,
        ),
        ("cuda13.3", "server"): (
            "audio-v0.9.3-bin-windows-x64-cuda13.3.zip",
            "0bd3988be10c31ec078c40df2bd0d4266e660804f6d5083aeb05d1309d091e43",
            277336614,
        ),
        ("cuda13.3", "runtime"): (
            "audio-v0.9.3-cudart-windows-x64-cuda13.3.zip",
            "839eb20f684a8aab64895b1e466b32ce9b29dfc19dc1ce15b2ef35218512ac0a",
            575457454,
        ),
    }
    assets = install.get("assets", [])
    expected_roles = ["server"] if variant == "cpu" else ["server", "runtime"]
    if sorted(asset.get("role", "") for asset in assets) != sorted(expected_roles):
        fail(f"{component_id}: CPU/CUDA asset pair is incomplete or mixed")
    for asset in assets:
        role = asset.get("role", "")
        expected = pins.get((variant, role))
        filename = asset.get("filename", "")
        if not expected or filename != expected[0] or asset.get("sha256") != expected[1]:
            fail(f"{component_id}: asset role, exact filename or SHA-256 is not allowlisted")
        expected_url = f"https://github.com/0xShug0/audio.cpp/releases/download/v0.9.3/{filename}"
        if asset.get("url") != expected_url:
            fail(f"{component_id}: asset URL must be the immutable official v0.9.3 release URL")
        size = asset.get("size_bytes")
        if not isinstance(size, int) or size != expected[2]:
            fail(f"{component_id}: asset byte size must exactly match the official release API pin")
    if metadata.get("family") != "audio.cpp" or metadata.get("version") != "v0.9.3" or metadata.get("backend") != variant:
        fail(f"{component_id}: engine metadata must pin audio.cpp v0.9.3 and selected backend")
    provenance = metadata.get("provenance", {})
    if (metadata.get("license") != "Apache-2.0" or
        provenance.get("source_url") != "https://github.com/0xShug0/audio.cpp" or
        provenance.get("revision") != "v0.9.3" or
        provenance.get("digest_policy") != "pinned-sha256-and-exact-size"):
        fail(f"{component_id}: audio.cpp license and immutable package provenance are required")
    if any(key in install for key in ("filename", "url", "sha256", "size_bytes", "command", "script")):
        fail(f"{component_id}: package set cannot contain single-file or executable installer fields")


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
    allowed_strategies = {"llama-release", "pinned-gguf", "audio-cpp-release-set"}
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
        allowed_install_keys = {"strategy", "variant", "filename", "url", "sha256", "size_bytes", "assets", "targets"}
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
            if not required_profile_keys.issubset(model_profile):
                fail(f"{component_id}: model_profile must include {sorted(required_profile_keys)}")
            allowed_profile_keys = required_profile_keys | {"recommended_for", "context_tokens", "preferred_context_tokens"}
            if set(model_profile) - allowed_profile_keys:
                fail(f"{component_id}: model_profile contains unknown properties {sorted(set(model_profile) - allowed_profile_keys)}")
            if model_profile.get("task_group") not in {"chat", "vision", "embeddings", "speech", "image-generation", "video", "audio"}:
                fail(f"{component_id}: unsupported model task_group")
            if model_profile.get("format") not in {"GGUF", "ONNX", "SafeTensors", "Other"}:
                fail(f"{component_id}: unsupported model format")
            for key in ("architecture", "parameter_count", "quantization"):
                if not isinstance(model_profile.get(key), str) or not model_profile[key].strip():
                    fail(f"{component_id}: model_profile.{key} must be a non-empty string")
            if "recommended_for" in model_profile and model_profile["recommended_for"] not in {"balanced", "alternative", "low-memory"}:
                fail(f"{component_id}: unsupported model_profile.recommended_for")
            for key in ("context_tokens", "preferred_context_tokens"):
                if key in model_profile and (not isinstance(model_profile[key], int) or model_profile[key] < 1 or model_profile[key] > 1048576):
                    fail(f"{component_id}: model_profile.{key} must be a bounded positive integer")
            if model_profile.get("preferred_context_tokens", 0) and model_profile.get("context_tokens", model_profile["preferred_context_tokens"]) < model_profile["preferred_context_tokens"]:
                fail(f"{component_id}: preferred context cannot exceed declared model context")
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
        elif strategy == "audio-cpp-release-set":
            validate_audio_cpp_contract(component_id, entry, metadata)
        if entry.get("default_model", False):
            if entry.get("kind") != "model" or strategy != "pinned-gguf":
                fail(f"{component_id}: only a pinned GGUF model can be the default model")
            defaults += 1

    if defaults != 1:
        fail(f"expected exactly one default model, got {defaults}")
    expected_curated_models = {"qwen3-4b-q4km", "gemma4-e2b-q4", "qwen2.5-1.5b-q4km"}
    actual_model_ids = {entry.get("id") for entry in components if entry.get("kind") == "model"}
    if not expected_curated_models.issubset(actual_model_ids):
        fail(f"curated local chat shortlist is incomplete: {sorted(expected_curated_models - actual_model_ids)}")
    if variants != {"cpu", "cuda12", "cuda13"}:
        fail(f"unexpected engine release variant set: {sorted(variants)}")

    active_audio = [entry for entry in components if entry.get("install", {}).get("strategy") == "audio-cpp-release-set"]
    audio_ids = {entry.get("id") for entry in active_audio}
    expected_audio_ids = {"audio.cpp-v0.9.3-cpu", "audio.cpp-v0.9.3-cuda13.3"}
    if len(active_audio) != 2 or audio_ids != expected_audio_ids:
        fail("audio.cpp catalog must expose only the exact-pinned Windows x64 CPU and CUDA 13.3 packages")
    audio_variants = {entry.get("install", {}).get("variant") for entry in active_audio}
    if audio_variants != {"cpu", "cuda13.3"}:
        fail("audio.cpp catalog must contain exactly one CPU and one CUDA 13.3 package set")
    for audio_entry in active_audio:
        audio_metadata = json.loads(audio_entry["metadata_json"])
        validate_audio_cpp_contract(audio_entry["id"], audio_entry, audio_metadata)
        expected_total = sum(asset["size_bytes"] for asset in audio_entry["install"]["assets"])
        if audio_metadata.get("resources", {}).get("download_bytes") != expected_total:
            fail(f"{audio_entry['id']}: displayed aggregate download bytes must match the exact asset set")
    cuda_catalog = next(entry for entry in active_audio if entry["id"] == "audio.cpp-v0.9.3-cuda13.3")
    cuda_metadata = json.loads(cuda_catalog["metadata_json"])
    if cuda_metadata.get("evidence", {}).get("real_task_test") != "not-verified" or cuda_metadata.get("evidence", {}).get("profile_validation") != "not-validated":
        fail("CUDA package must not claim a real task or target profile has already passed")

    qwen_asr = next((entry for entry in components if entry.get("id") == "qwen3-asr-0.6b-q8_0"), None)
    if qwen_asr is None:
        fail("the exact-pinned Qwen3-ASR 0.6B Q8_0 model must be present in the catalog")
    qwen_asr_metadata = json.loads(qwen_asr["metadata_json"])
    qwen_asr_install = qwen_asr.get("install", {})
    if (qwen_asr_install.get("filename") != "qwen3-asr-0.6b-q8_0.gguf" or
        qwen_asr_install.get("url") != "https://huggingface.co/audio-cpp/audio.cpp-gguf/resolve/4c9a3098d552a0f3d9b2adc11638a11022c07d7e/Qwen3-ASR-0.6B-GGUF/qwen3-asr-0.6b-q8_0.gguf" or
        qwen_asr_install.get("sha256") != "6c44ec2fb4cee513892d7863c1fcc3ea6b699ffa4d899b0ef4ab19956d9544f7" or
        qwen_asr_install.get("size_bytes") != 1151272416):
        fail("Qwen3-ASR model must retain the reviewed immutable revision, exact filename, byte size and SHA-256")
    if (qwen_asr_metadata.get("license") != "Apache-2.0" or
        qwen_asr_metadata.get("provenance", {}).get("revision") != "4c9a3098d552a0f3d9b2adc11638a11022c07d7e" or
        qwen_asr_metadata.get("tasks") != ["asr.transcribe"] or
        qwen_asr_metadata.get("model_profile", {}).get("quantization") != "Q8_0" or
        qwen_asr_metadata.get("compatibility", {}).get("backends") != ["audio.cpp-cpu", "audio.cpp-cuda13.3"]):
        fail("Qwen3-ASR metadata must declare reviewed Apache-2.0 provenance, ASR task, Q8_0 and audio.cpp targets")
    if qwen_asr_metadata.get("evidence", {}).get("real_task_test") != "not-verified-on-target":
        fail("Qwen3-ASR catalog metadata must not claim the target transcription task has already passed")

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
    # Synthetic package-set fixtures exercise CPU single-asset and CUDA pair policy
    # without advertising unverified byte sizes in the shipped catalog.
    base_audio_metadata = {
        "id": "audio-cpp-cpu", "type": "engine", "family": "audio.cpp",
        "version": "v0.9.3", "backend": "cpu", "license": "Apache-2.0",
        "provenance": {"source_url": "https://github.com/0xShug0/audio.cpp", "revision": "v0.9.3",
                       "digest_policy": "pinned-sha256-and-exact-size"},
    }
    cpu_file = "audio-v0.9.3-bin-windows-x64-cpu.zip"
    cpu_entry = {"kind": "engine", "default_model": False,
                 "install": {"strategy": "audio-cpp-release-set", "variant": "cpu",
                             "targets": [{"platform": "windows", "architecture": "x64"}],
                             "assets": [{"role": "server", "filename": cpu_file,
                                         "url": f"https://github.com/0xShug0/audio.cpp/releases/download/v0.9.3/{cpu_file}",
                                         "sha256": "3ac66f46395b4e92e4f3bb68b200f0032310bd9922f12d97f10ccdadb02dee4b",
                                         "size_bytes": 30547092}]}}
    validate_audio_cpp_contract("audio-cpp-cpu", cpu_entry, base_audio_metadata)
    wrong_cpu_size = copy.deepcopy(cpu_entry)
    wrong_cpu_size["install"]["assets"][0]["size_bytes"] += 1
    try:
        validate_audio_cpp_contract("audio-cpp-cpu", wrong_cpu_size, base_audio_metadata)
    except SystemExit:
        pass
    else:
        fail("non-exact CPU archive byte size was accepted")
    cuda_metadata = {**base_audio_metadata, "id": "audio-cpp-cuda", "backend": "cuda13.3"}
    cuda_entry = copy.deepcopy(cpu_entry)
    cuda_entry["install"]["variant"] = "cuda13.3"
    cuda_server = "audio-v0.9.3-bin-windows-x64-cuda13.3.zip"
    cuda_runtime = "audio-v0.9.3-cudart-windows-x64-cuda13.3.zip"
    cuda_entry["install"]["assets"] = [
        {"role": "server", "filename": cuda_server,
         "url": f"https://github.com/0xShug0/audio.cpp/releases/download/v0.9.3/{cuda_server}",
         "sha256": "0bd3988be10c31ec078c40df2bd0d4266e660804f6d5083aeb05d1309d091e43", "size_bytes": 277336614},
        {"role": "runtime", "filename": cuda_runtime,
         "url": f"https://github.com/0xShug0/audio.cpp/releases/download/v0.9.3/{cuda_runtime}",
         "sha256": "839eb20f684a8aab64895b1e466b32ce9b29dfc19dc1ce15b2ef35218512ac0a", "size_bytes": 575457454},
    ]
    validate_audio_cpp_contract("audio-cpp-cuda", cuda_entry, cuda_metadata)
    invalid_cuda = copy.deepcopy(cuda_entry)
    invalid_cuda["install"]["assets"].pop()
    try:
        validate_audio_cpp_contract("audio-cpp-cuda", invalid_cuda, cuda_metadata)
    except SystemExit:
        pass
    else:
        fail("incomplete CUDA package set was accepted")
    print(f"Catalog manifest contract passed: {len(components)} reviewed components, {len(ids)} unique IDs, {len(variants)} llama.cpp engine variants and exact-pinned audio.cpp CPU/CUDA 13.3 package sets; GGUF pinning and tamper fixtures passed.")


if __name__ == "__main__":
    main()
