#!/usr/bin/env python3
"""End-to-end smoke test: runtime -> child process -> local OpenAI-compatible API."""
import argparse
import json
import socket
import subprocess
import shutil
import os
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

from catalog_contract import validate_catalog_schema


def reserve_distinct_ports():
    """Reserve three different ephemeral ports for runtime, fake engine and setup config."""
    sockets = []
    try:
        ports = []
        for _ in range(3):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.bind(("127.0.0.1", 0))
            sockets.append(sock)
            ports.append(sock.getsockname()[1])
        if len(set(ports)) != 3:
            raise RuntimeError("OS returned duplicate ports for the HTTP smoke test")
        return tuple(ports)
    finally:
        for sock in sockets:
            sock.close()


def request(url, method="GET", body=None, timeout=2):
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=timeout) as response:
        raw = response.read().decode("utf-8")
        content_type = response.headers.get("Content-Type", "")
        return response.status, raw, content_type


def wait_until(check, timeout_s, description):
    end = time.monotonic() + timeout_s
    last_error = None
    while time.monotonic() < end:
        try:
            value = check()
            if value:
                return value
        except (OSError, urllib.error.URLError, TimeoutError, ValueError) as exc:
            last_error = exc
        time.sleep(0.15)
    raise RuntimeError(f"Timed out waiting for {description}: {last_error}")


def expect_forbidden(url, headers, description):
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=2) as response:
            raise AssertionError(f"{description} should be blocked, got HTTP {response.status}")
    except urllib.error.HTTPError as exc:
        if exc.code != 403:
            raise AssertionError(f"{description} should return 403, got HTTP {exc.code}") from exc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--fake-engine", required=True)
    parser.add_argument("--web-dir", required=True)
    args = parser.parse_args()

    runtime = str(Path(args.runtime).resolve())
    fake_engine = str(Path(args.fake_engine).resolve())
    web_dir = str(Path(args.web_dir).resolve())
    for path in (runtime, fake_engine, web_dir):
        if not Path(path).exists():
            raise RuntimeError(f"Required smoke-test path does not exist: {path}")

    runtime_port, engine_port, setup_engine_port = reserve_distinct_ports()
    with tempfile.TemporaryDirectory(prefix="lar-http-smoke-") as temporary:
        temp = Path(temporary)
        test_web_dir = temp / "web"
        shutil.copytree(web_dir, test_web_dir)
        web_dir = str(test_web_dir)
        config_path = temp / "engines.json"
        engine_working_directory = temp / "engine-cwd"
        engine_working_directory.mkdir()
        setup_bin_dir = temp / "setup-bin"
        setup_bin_dir.mkdir()
        setup_binary = setup_bin_dir / ("llama-server.exe" if os.name == "nt" else "llama-server")
        shutil.copy2(fake_engine, setup_binary)
        if os.name != "nt":
            setup_binary.chmod(setup_binary.stat().st_mode | 0o111)
        model_path = temp / "test-model.gguf"
        model_path.write_bytes(b"GGUF" + b" CI placeholder, not a real model")
        config = {
            "server": {"host": "127.0.0.1", "port": runtime_port},
            "engines": [{
                "id": "fake-engine",
                "display_name": "CI fake OpenAI-compatible engine",
                "executable_path": fake_engine,
                "model_path": str(model_path),
                "working_directory": str(engine_working_directory),
                "args": ["--port", str(engine_port)],
                "upstream_base_url": f"http://127.0.0.1:{engine_port}",
                "capabilities": ["chat"],
                "default_for_chat": True
            }]
        }
        config_path.write_text(json.dumps(config), encoding="utf-8")
        base = f"http://127.0.0.1:{runtime_port}"
        engine_base = f"http://127.0.0.1:{engine_port}"
        runtime_log_path = temp / "runtime.log"
        runtime_log = runtime_log_path.open("w", encoding="utf-8")
        # Force the runtime to use an explicit root that differs from both
        # platform defaults. This mirrors the installed desktop shell passing
        # the same per-user path to the launcher/runtime, and catches catalog
        # or diagnostics silently reading a fresh/empty directory after upgrade.
        data_root = temp / "explicit-user-data-root"
        if os.name == "nt":
            default_data_root = temp / "local-app-data" / "LocalAIRuntime"
        else:
            default_data_root = temp / "xdg-data" / "local-ai-runtime"
        runtime_instance_id = str(uuid.uuid4())
        engine_fixture = data_root / "engines" / "llama.cpp" / "state-contract-fixture-cpu"
        engine_fixture.mkdir(parents=True, exist_ok=True)
        installed_server = engine_fixture / ("llama-server.exe" if os.name == "nt" else "llama-server")
        shutil.copy2(fake_engine, installed_server)
        if os.name != "nt":
            installed_server.chmod(installed_server.stat().st_mode | 0o111)
        engine_log_for_diagnostics = data_root / "logs" / "fake-engine.log"
        engine_log_for_diagnostics.parent.mkdir(parents=True, exist_ok=True)
        engine_log_for_diagnostics.write_text("engine-log-diagnostics-smoke-marker\n", encoding="utf-8")
        test_env = os.environ.copy()
        test_env["LOCALAPPDATA"] = str(temp / "local-app-data")
        test_env["XDG_DATA_HOME"] = str(temp / "xdg-data")
        test_env["LOCAL_AI_RUNTIME_DATA_DIR"] = str(data_root)
        test_env["LOCAL_AI_RUNTIME_DATA_ROOT_ID"] = runtime_instance_id
        process = subprocess.Popen(
            [runtime, "--config", str(config_path), "--web-dir", web_dir],
            cwd=str(temp), stdout=runtime_log, stderr=subprocess.STDOUT, env=test_env,
        )
        try:
            wait_until(lambda: request(base + "/health")[0] == 200, 15, "runtime health endpoint")

            # The desktop shell serves only the two reviewed locale resources.
            locale_resources = {}
            for locale in ("en", "tr"):
                locale_status, locale_raw, locale_type = request(base + f"/locales/{locale}.json")
                assert locale_status == 200 and "application/json" in locale_type
                locale_resources[locale] = json.loads(locale_raw)
            assert set(locale_resources["en"]) == set(locale_resources["tr"]), "English and Turkish resources must expose the same keys"
            assert locale_resources["en"].get("nav.overview") == "Overview"
            assert locale_resources["tr"].get("nav.overview") == "Genel Bakış"
            try:
                request(base + "/locales/fr.json")
                raise AssertionError("The locale route must not expose arbitrary file paths.")
            except urllib.error.HTTPError as locale_error:
                assert locale_error.code == 404

            status, identity_raw, identity_type = request(base + "/v1/system/identity")
            identity = json.loads(identity_raw)
            assert status == 200 and "application/json" in identity_type
            assert identity.get("schema_version") == 1 and identity.get("data_root_id") == runtime_instance_id
            explicit_runtime_log = data_root / "logs" / "runtime.log"
            assert explicit_runtime_log.exists(), "Runtime diagnostics did not use LOCAL_AI_RUNTIME_DATA_DIR"
            assert not (default_data_root / "logs" / "runtime.log").exists(), (
                "Runtime unexpectedly created diagnostics in the platform-default user-data root"
            )

            # A duplicate instance must exit cleanly and leave a startup-failure
            # event in the local diagnostics log instead of looking like a hang.
            try:
                duplicate = subprocess.run(
                    [runtime, "--config", str(config_path), "--web-dir", web_dir],
                    cwd=str(temp), env=test_env, capture_output=True, text=True, timeout=5,
                )
            except subprocess.TimeoutExpired as exc:
                raise AssertionError(
                    "A duplicate runtime hung instead of rejecting the occupied port promptly; "
                    "check platform socket address-reuse behavior."
                ) from exc
            assert duplicate.returncode != 0, "A second runtime should not bind the same port"
            persisted_runtime_log = data_root / "logs" / "runtime.log"
            startup_events = persisted_runtime_log.read_text(encoding="utf-8", errors="replace")
            assert "runtime.startup_failed" in startup_events, startup_events

            expect_forbidden(
                base + "/health",
                {"Host": f"attacker.invalid:{runtime_port}"},
                "untrusted Host header",
            )
            expect_forbidden(
                base + "/health",
                {"Origin": "https://attacker.invalid"},
                "cross-origin browser request",
            )
            status, page, content_type = request(base + "/")
            assert status == 200 and "text/html" in content_type and "Local AI Runtime" in page
            assert 'id="workspace-panel"' in page and "Language &amp; chat" in page
            assert "Image generation" in page and "Vision &amp; OCR" in page and "Speech &amp; audio" in page
            assert "Only available workspaces are interactive." in page
            assert 'data-view-target="overview"' in page and 'data-view-target="chat"' in page
            assert 'data-view-target="models"' in page and 'data-view-target="activity"' in page
            assert 'data-view-target="settings"' in page
            assert 'id="diagnostics-export"' in page and 'id="downloads-refresh"' in page
            assert 'id="include-engine-logs"' in page
            assert 'id="update-download"' in page
            assert 'id="update-apply"' in page

            status, diagnostics_raw, diagnostics_type = request(base + "/v1/diagnostics/export")
            assert status == 200 and "text/plain" in diagnostics_type
            assert "Remote telemetry: disabled" in diagnostics_raw and "runtime.started" in diagnostics_raw
            assert "Engine process logs: not included" in diagnostics_raw
            status, diagnostics_with_engine_logs, _ = request(base + "/v1/diagnostics/export?include_engine_logs=1")
            assert status == 200 and "engine-log-diagnostics-smoke-marker" in diagnostics_with_engine_logs, diagnostics_with_engine_logs
            status, event_raw, event_type = request(base + "/v1/diagnostics/events", method="POST", body={
                "event": "ui.smoke_test", "detail": "diagnostic endpoint smoke marker",
            })
            assert status == 202 and "application/json" in event_type
            status, diagnostics_raw, _ = request(base + "/v1/diagnostics/export")
            assert "ui.smoke_test" in diagnostics_raw and "diagnostic endpoint smoke marker" in diagnostics_raw

            status, hardware_raw, hardware_type = request(base + "/v1/system/hardware")
            hardware_info = json.loads(hardware_raw)
            assert status == 200 and "application/json" in hardware_type
            assert hardware_info["platform"] and hardware_info["architecture"]
            assert isinstance(hardware_info["logical_cpu_count"], int) and hardware_info["logical_cpu_count"] >= 1
            assert isinstance(hardware_info["total_memory_bytes"], int)
            assert isinstance(hardware_info["available_memory_bytes"], int)
            assert isinstance(hardware_info["available_disk_bytes"], int)
            assert hardware_info["gpu_probe_status"] in ("complete", "partial", "unsupported")
            assert isinstance(hardware_info["gpus"], list)
            assert hardware_info["gpu_use_requires_validation"] is True
            hardware_recommendations = hardware_info.get("recommendations")
            assert isinstance(hardware_recommendations, list) and len(hardware_recommendations) >= 2
            cpu_recommendation = next((item for item in hardware_recommendations if item.get("id") == "starter-chat-cpu"), None)
            cuda_recommendation = next((item for item in hardware_recommendations if item.get("id") == "nvidia-cuda-chat"), None)
            assert cpu_recommendation is not None and cpu_recommendation.get("backend") == "CPU"
            assert cpu_recommendation.get("status") in ("recommended", "caution", "resource-constrained", "needs-space", "unknown")
            assert cpu_recommendation.get("evidence") == "hardware-heuristic"
            assert isinstance(cpu_recommendation.get("download_bytes"), int) and cpu_recommendation["download_bytes"] > 0
            assert cuda_recommendation is not None and cuda_recommendation.get("backend") == "NVIDIA CUDA"
            assert cuda_recommendation.get("status") in ("candidate", "review", "not-detected", "unknown")
            assert cuda_recommendation.get("evidence") == "hardware-screen-only"
            assert "not" in cuda_recommendation.get("reason", "").lower() or "candidate" in cuda_recommendation.get("reason", "").lower() or "adapter" in cuda_recommendation.get("reason", "").lower()
            status, hardware_diagnostics, _ = request(base + "/v1/diagnostics/export")
            assert status == 200 and "hardware.snapshot" in hardware_diagnostics

            status, version_raw, content_type = request(base + "/version")
            version_info = json.loads(version_raw)
            assert status == 200 and "application/json" in content_type
            assert version_info["version"] and version_info["repository"].endswith("/local-ai-runtime")
            assert "releases_api_url" in version_info and "api.github.com" in version_info["releases_api_url"]
            assert isinstance(version_info.get("desktop_shell_installed"), bool), version_info
            assert "desktop_installer_asset_name" in version_info, version_info
            assert version_info["desktop_shell_installed"] is False
            if sys.platform == "win32":
                expected_update_asset = "local-ai-runtime-windows-x64.zip"
            elif sys.platform == "darwin":
                machine = os.uname().machine.lower()
                expected_update_asset = "local-ai-runtime-macos-arm64.tar.gz" if machine in ("arm64", "aarch64") else "local-ai-runtime-macos-x64.tar.gz"
            elif sys.platform.startswith("linux"):
                expected_update_asset = "local-ai-runtime-linux-x64.tar.gz" if os.uname().machine.lower() in ("x86_64", "amd64") else ""
            else:
                expected_update_asset = ""
            assert version_info.get("update_asset_name", "") == expected_update_asset, version_info
            assert isinstance(version_info.get("update_apply_supported"), bool), version_info
            if sys.platform == "win32":
                desktop_marker = Path(web_dir) / "desktop-shell.json"
                desktop_marker.write_text('{"kind":"installed-desktop"}', encoding="utf-8")
                status, desktop_version_raw, _ = request(base + "/version")
                desktop_version = json.loads(desktop_version_raw)
                assert status == 200 and desktop_version["desktop_shell_installed"] is True
                assert desktop_version["desktop_installer_asset_name"] == "local-ai-runtime-windows-x64-setup.exe"
                assert desktop_version["update_apply_supported"] is False, "Desktop installs must not apply the portable ZIP in place"
                desktop_marker.unlink()
            if str(version_info.get("version", "")).endswith("-dev"):
                assert version_info["update_apply_supported"] is False, "Development previews must never offer in-place updates"

            status, staged_status_raw, staged_status_type = request(
                base + "/v1/updates/status", method="POST", body={"tag_name": "v987.654.321-smoke"},
            )
            staged_status = json.loads(staged_status_raw)
            assert status == 200 and "application/json" in staged_status_type
            assert staged_status["tag_name"] == "v987.654.321-smoke" and staged_status["staged"] is False
            assert staged_status["verified"] is False and staged_status["apply_supported"] is False
            try:
                request(base + "/v1/updates/status", method="POST", body={"tag_name": "../invalid"})
                raise AssertionError("Update status must reject path-like release tags")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for an invalid status tag, got {exc.code}"

            # In development/build-tree layouts the endpoint must fail closed;
            # Windows apply is exposed only by a tagged portable release package.
            try:
                request(base + "/v1/updates/apply", method="POST", body={"tag_name": "v987.654.321-smoke"})
                raise AssertionError("Apply-and-restart must be unavailable outside a supported portable release")
            except urllib.error.HTTPError as exc:
                assert exc.code == 501, f"Expected 501 for unsupported update apply, got {exc.code}"

            # The updater must fail closed on a release URL outside the exact
            # official asset path and on absent/invalid checksums. These tests do
            # not access the network or create staged packages.
            update_payload = {
                "tag_name": "v9.0.0",
                "asset_name": expected_update_asset,
                "browser_download_url": "https://example.invalid/runtime-package.zip",
                "digest": "sha256:" + ("a" * 64),
                "size": 4000000,
            }
            try:
                request(base + "/v1/updates/stage", method="POST", body=update_payload)
                raise AssertionError("Runtime update staging must reject a non-official URL")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for non-official update URL, got {exc.code}"
            update_payload["browser_download_url"] = f"https://github.com/azmisahin-ai/local-ai-runtime/releases/download/v9.0.0/{expected_update_asset}"
            update_payload["digest"] = "sha256:not-a-valid-sha256"
            try:
                request(base + "/v1/updates/stage", method="POST", body=update_payload)
                raise AssertionError("Runtime update staging must reject invalid digests")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for invalid update digest, got {exc.code}"
            update_payload["tag_name"] = "v0.0.1"
            update_payload["browser_download_url"] = f"https://github.com/azmisahin-ai/local-ai-runtime/releases/download/v0.0.1/{expected_update_asset}"
            update_payload["digest"] = "sha256:" + ("a" * 64)
            try:
                request(base + "/v1/updates/stage", method="POST", body=update_payload)
                raise AssertionError("Runtime update staging must reject older release tags")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for older update tag, got {exc.code}"
            status, jobs_raw, _ = request(base + "/v1/downloads")
            assert status == 200 and json.loads(jobs_raw)["data"] == [], "Rejected update requests must not start a job"

            # Managed catalog and download job endpoints must be safe to inspect
            # without network side effects. Downloads require explicit license
            # acknowledgement and only allow built-in, reviewed item IDs.
            status, catalog_raw, catalog_type = request(base + "/v1/catalog")
            catalog = json.loads(catalog_raw)
            assert status == 200 and "application/json" in catalog_type
            catalog_schema_path = Path(__file__).resolve().parents[1] / "catalog" / "schemas" / "component-v1.schema.json"
            catalog_schema = json.loads(catalog_schema_path.read_text(encoding="utf-8"))
            catalog_errors = validate_catalog_schema(catalog, catalog_schema)
            assert not catalog_errors, "Catalog API did not satisfy component-v1 schema: " + "; ".join(catalog_errors[:12])
            activity_status, activity_raw, activity_type = request(base + "/v1/activity")
            activity = json.loads(activity_raw)
            activity_schema_path = Path(__file__).resolve().parents[1] / "catalog" / "schemas" / "activity-v1.schema.json"
            activity_schema = json.loads(activity_schema_path.read_text(encoding="utf-8"))
            activity_errors = validate_catalog_schema(activity, activity_schema)
            assert activity_status == 200 and "application/json" in activity_type
            assert not activity_errors, "Activity API did not satisfy activity-v1 schema: " + "; ".join(activity_errors[:12])
            assert activity["snapshot"] is True and activity["summary"]["engine_count"] == 1
            items = {item["id"]: item for item in catalog["data"]}
            assert "qwen2.5-1.5b-q4km" in items
            model_item = items["qwen2.5-1.5b-q4km"]
            assert model_item["license"] == "Apache-2.0"
            assert model_item["size_bytes"] == 1117320736
            assert model_item["sha256"] == "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e"
            assert model_item["family"] == "Qwen2.5"
            assert model_item["resources"]["estimate_confidence"] == "heuristic"
            assert model_item["provenance"]["revision_policy"] == "pinned-revision"
            assert "llama.cpp-cpu" in items
            assert items["llama.cpp-cpu"]["compatibility"]["backends"] == ["cpu"]
            cpu_item = items["llama.cpp-cpu"]
            assert cpu_item["installed"] is True, (
                "Catalog did not discover the installed fixture under the explicit user-data root: " + json.dumps(cpu_item)
            )
            assert Path(cpu_item["installed_path"]).resolve() == engine_fixture.resolve(), (
                "Catalog reported an engine from a different user-data root: " + cpu_item["installed_path"]
            )
            if "llama.cpp-cuda12" in items:
                cuda_dependencies = items["llama.cpp-cuda12"]["provenance"]["dependencies"]
                assert any(dependency["id"] == "llama.cpp-cudart-cuda12" for dependency in cuda_dependencies)
                assert items["llama.cpp-cuda12"]["evidence"]["real_task_test"] == "not-verified-by-hosted-ci"
            status, jobs_raw, _ = request(base + "/v1/downloads")
            assert status == 200 and json.loads(jobs_raw)["data"] == []
            try:
                request(base + "/v1/downloads/qwen2.5-1.5b-q4km", method="POST", body={"accept_license": False})
                raise AssertionError("Download must require explicit license acknowledgement")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for missing license acknowledgement, got {exc.code}"
            try:
                request(base + "/v1/downloads/not-in-catalog", method="POST", body={"accept_license": True})
                raise AssertionError("Unknown catalog items must be rejected")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for unknown catalog item, got {exc.code}"
            status, jobs_raw, _ = request(base + "/v1/downloads")
            assert status == 200 and json.loads(jobs_raw)["data"] == [], "Rejected downloads must not create jobs or start network requests"

            status, engines_raw, _ = request(base + "/v1/engines")
            engines = json.loads(engines_raw)["data"]
            assert status == 200 and len(engines) == 1 and engines[0]["status"] == "stopped"

            request(base + "/v1/engines/fake-engine/start", method="POST", body={})

            # The start endpoint confirms process creation, not application
            # readiness. The native C++ fake engine avoids Python-launcher/environment
            # differences on hosted macOS runners and tests the actual child-process path.
            try:
                wait_until(
                    lambda: request(engine_base + "/v1/models")[0] == 200,
                    10,
                    "fake engine readiness",
                )
                engine_log_path = engine_log_for_diagnostics
                engine_log = engine_log_path.read_text(encoding="utf-8", errors="replace")
                assert "FAKE_ENGINE_READY" in engine_log, f"Fake engine startup marker missing from {engine_log_path}: {engine_log}"
                assert str(engine_working_directory) in engine_log, (
                    "Fake engine did not run in its configured working directory: " + engine_log
                )
                status, engine_status_raw, _ = request(base + "/v1/engines")
                engine_status = json.loads(engine_status_raw)["data"][0]
                assert status == 200 and engine_status["ready"] is True, (
                    "Runtime should distinguish process-running from HTTP-ready: " + engine_status_raw
                )
                activity_status, activity_raw, _ = request(base + "/v1/activity")
                activity = json.loads(activity_raw)
                ready_items = [item for item in activity["data"] if item["type"] == "engine" and item["component_id"] == "fake-engine"]
                assert activity_status == 200 and len(ready_items) == 1 and ready_items[0]["state"] == "ready"
                assert activity["summary"]["ready_engines"] == 1
                assert engine_status["backend_target"] == "unspecified"
                # GPU observation must refuse to run under an unverified/non-GPU
                # engine configuration; it must not silently change the backend.
                try:
                    request(base + "/v1/diagnostics/gpu-observation", method="POST", body={}, timeout=5)
                    raise AssertionError("GPU observation must require an explicit CUDA/GPU engine configuration")
                except urllib.error.HTTPError as observation_error:
                    observation_body = json.loads(observation_error.read().decode("utf-8"))
                    assert observation_error.code == 409, observation_body
                    assert "GPU layers to auto" in observation_body.get("error", {}).get("message", ""), observation_body
                assert engine_status["gpu_execution_evidence"] == "not-verified"
                assert isinstance(engine_status["gpu_execution_evidence_summary"], str)
                assert isinstance(engine_status["engine_log_available"], bool)
                assert isinstance(engine_status["gpu_evidence_current_session"], bool)
                assert isinstance(engine_status["cuda_runtime_reported"], bool)
                try:
                    request(base + "/v1/setup/llama-cpp", method="POST", body={
                        "executable_path": str(setup_binary),
                        "model_path": str(model_path),
                        "backend": "cpu", "context_size": 2048, "engine_port": setup_engine_port,
                    })
                    raise AssertionError("Setup must not replace configuration while an engine is running")
                except urllib.error.HTTPError as setup_error:
                    assert setup_error.code == 409, f"Expected 409 while engine is running, got {setup_error.code}"
            except Exception:
                # Include the process state alongside stderr/stdout so a spawn
                # failure, a crashed engine, and a reachable-but-unready engine
                # can be distinguished from one another in CI logs.
                try:
                    status, status_raw, _ = request(base + "/v1/engines", timeout=1)
                    print(f"Engine status after readiness failure (HTTP {status}): {status_raw}", file=sys.stderr)
                except Exception as status_error:
                    print(f"Could not query engine status after readiness failure: {status_error}", file=sys.stderr)
                raise

            def models_ready():
                status, raw, _ = request(base + "/v1/models", timeout=1)
                return json.loads(raw) if status == 200 else None

            models = wait_until(models_ready, 10, "proxied model list")
            assert models["data"][0]["id"] == "fake-model"

            status, raw, content_type = request(
                base + "/v1/chat/completions", method="POST",
                body={"model": "fake-model", "messages": [{"role": "user", "content": "hello"}]},
            )
            assert status == 200 and "application/json" in content_type
            result = json.loads(raw)
            assert result["choices"][0]["message"]["content"] == "LAR_HTTP_SMOKE_OK"

            request(base + "/v1/engines/fake-engine/stop", method="POST", body={})
            status, engines_raw, _ = request(base + "/v1/engines")
            engines = json.loads(engines_raw)["data"]
            assert status == 200 and engines[0]["status"] == "stopped"

            # The setup wizard accepts only an explicitly selected llama-server binary
            # and a non-empty GGUF file, persists configuration, and updates live state.
            try:
                request(base + "/v1/setup/llama-cpp", method="POST", body={
                    "executable_path": str(setup_binary),
                    "model_path": str(temp / "missing.gguf"),
                    "backend": "cpu", "context_size": 2048, "engine_port": setup_engine_port,
                })
                raise AssertionError("Setup should reject a model path that does not exist")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for invalid model path, got {exc.code}"

            # Reject a regular executable with the wrong basename, even if it exists.
            wrong_binary = setup_bin_dir / ("other-program.exe" if os.name == "nt" else "other-program")
            shutil.copy2(fake_engine, wrong_binary)
            if os.name != "nt":
                wrong_binary.chmod(wrong_binary.stat().st_mode | 0o111)
            try:
                request(base + "/v1/setup/llama-cpp", method="POST", body={
                    "executable_path": str(wrong_binary),
                    "model_path": str(model_path),
                    "backend": "cpu", "context_size": 2048, "engine_port": setup_engine_port,
                })
                raise AssertionError("Setup should reject a binary that is not named llama-server")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for invalid executable name, got {exc.code}"

            invalid_signature_model = temp / "bad-signature.gguf"
            invalid_signature_model.write_bytes(b"NOPE" + b" placeholder bytes")
            try:
                request(base + "/v1/setup/llama-cpp", method="POST", body={
                    "executable_path": str(setup_binary),
                    "model_path": str(invalid_signature_model),
                    "backend": "cpu", "context_size": 2048, "engine_port": setup_engine_port,
                })
                raise AssertionError("Setup should reject a .gguf file with an invalid signature")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for invalid GGUF signature, got {exc.code}"

            binary_model = temp / "not-a-gguf.bin"
            binary_model.write_bytes(b"GGUF" + b" not a GGUF model by extension")
            try:
                request(base + "/v1/setup/llama-cpp", method="POST", body={
                    "executable_path": str(setup_binary),
                    "model_path": str(binary_model),
                    "backend": "cpu", "context_size": 2048, "engine_port": setup_engine_port,
                })
                raise AssertionError("Setup should reject a model file that does not use .gguf")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for wrong model extension, got {exc.code}"

            try:
                request(base + "/v1/setup/llama-cpp", method="POST", body={
                    "executable_path": str(setup_binary),
                    "model_path": str(model_path),
                    "backend": "cpu", "context_size": 2048, "engine_port": runtime_port,
                })
                raise AssertionError("Setup should reject an engine port that conflicts with runtime")
            except urllib.error.HTTPError as exc:
                assert exc.code == 422, f"Expected 422 for conflicting port, got {exc.code}"

            status, setup_raw, content_type = request(base + "/v1/setup/llama-cpp", method="POST", body={
                "executable_path": f'"{setup_binary}"',
                "model_path": f'"{model_path}"',
                "backend": "cpu", "context_size": 2048, "engine_port": setup_engine_port,
            })
            setup_result = json.loads(setup_raw)
            assert status == 200 and "application/json" in content_type
            assert setup_result["id"] == "llama-cpp" and setup_result["config_saved"] is True
            persisted = json.loads(config_path.read_text(encoding="utf-8"))
            configured_llama = next(item for item in persisted["engines"] if item["id"] == "llama-cpp")
            assert configured_llama["executable_path"] == str(setup_binary)
            assert configured_llama["model_path"] == str(model_path)
            assert configured_llama["default_for_chat"] is True
            assert "--n-gpu-layers" in configured_llama["args"] and "0" in configured_llama["args"]
            assert configured_llama["args"][configured_llama["args"].index("--cors-origins") + 1] == "localhost"
            assert "--no-cors-credentials" in configured_llama["args"]
            status, engines_raw, _ = request(base + "/v1/engines")
            engines = json.loads(engines_raw)["data"]
            llama_status = next(item for item in engines if item["id"] == "llama-cpp")
            assert status == 200 and llama_status["model_configured"] is True and llama_status["default_for_chat"] is True
            assert llama_status["status"] == "stopped" and llama_status["ready"] is False
            assert llama_status["backend_target"] == "cpu"
            assert llama_status["gpu_execution_evidence"] == "not-verified"
            assert isinstance(llama_status["gpu_execution_evidence_summary"], str)

            # GPU mode must use llama.cpp's automatic placement rather than
            # blindly forcing every layer onto a potentially small GPU.
            status, gpu_raw, _ = request(base + "/v1/setup/llama-cpp", method="POST", body={
                "executable_path": str(setup_binary),
                "model_path": str(model_path),
                "backend": "gpu", "context_size": 2048, "engine_port": setup_engine_port,
            })
            assert status == 200 and json.loads(gpu_raw)["backend"] == "gpu"
            persisted = json.loads(config_path.read_text(encoding="utf-8"))
            configured_llama = next(item for item in persisted["engines"] if item["id"] == "llama-cpp")
            assert "--n-gpu-layers" in configured_llama["args"]
            assert "auto" in configured_llama["args"], "GPU setup should use automatic layer placement"
            status, engine_status_raw, _ = request(base + "/v1/engines")
            engine_status = next(item for item in json.loads(engine_status_raw)["data"] if item["id"] == "llama-cpp")
            assert status == 200 and engine_status["backend_target"] == "gpu-requested"
            assert engine_status["gpu_execution_evidence"] == "not-verified", "GPU request must not be mislabeled as verified execution"
            assert isinstance(engine_status["gpu_evidence_current_session"], bool), "status should expose whether evidence is scoped to the latest managed process session"
            assert configured_llama["args"][configured_llama["args"].index("--cors-origins") + 1] == "localhost"
            assert "--no-cors-credentials" in configured_llama["args"]
            denied_shutdown = urllib.request.Request(
                base + "/v1/runtime/shutdown", data=b"{}", method="POST",
                headers={"Content-Type": "application/json", "Origin": "https://attacker.invalid"},
            )
            try:
                urllib.request.urlopen(denied_shutdown, timeout=2)
                raise AssertionError("Cross-origin shutdown requests must be rejected")
            except urllib.error.HTTPError as exc:
                assert exc.code == 403, f"Expected 403 for cross-origin shutdown, got HTTP {exc.code}"
            assert process.poll() is None, "Rejected cross-origin request must not shut down the runtime"

            status, shutdown_raw, _ = request(base + "/v1/runtime/shutdown", method="POST", body={})
            assert status == 202 and json.loads(shutdown_raw)["status"] == "shutting-down"
            wait_until(lambda: process.poll() is not None, 5, "graceful runtime shutdown")
            assert process.returncode == 0, f"Runtime exited with {process.returncode} after graceful shutdown"
            print("HTTP smoke test passed: dashboard, local hardware inventory, health/version/platform-update metadata, staged update status, apply capability guards, update URL/digest guards, duplicate-startup diagnostics, security guards, readiness probe, engine lifecycle, chat/models proxy, GGUF validation, quoted paths, restricted engine CORS, CPU/GPU setup persistence, graceful shutdown.")
        except Exception:
            runtime_log.flush()
            print(
                f"HTTP smoke diagnostics: runtime_port={runtime_port}, "
                f"engine_port={engine_port}",
                file=sys.stderr,
            )
            try:
                runtime_output = runtime_log_path.read_text(encoding="utf-8", errors="replace")
                if runtime_output.strip():
                    print("--- runtime log ---", file=sys.stderr)
                    print(runtime_output, file=sys.stderr)
            except OSError:
                pass
            engine_log_path = engine_log_for_diagnostics
            if engine_log_path.exists():
                print("--- fake engine log ---", file=sys.stderr)
                print(engine_log_path.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            raise
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            runtime_log.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"HTTP smoke test failed: {exc}", file=sys.stderr)
        raise
