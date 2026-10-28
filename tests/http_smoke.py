#!/usr/bin/env python3
"""End-to-end smoke test: runtime -> child process -> local OpenAI-compatible API."""
import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def reserve_distinct_ports():
    """Reserve two different ephemeral ports at the same time.

    Requesting a port, closing its socket, and immediately requesting another
    port can return the same number on some OS runners. That would make the
    fake engine point back at the runtime itself and can hang the proxy test.
    Keep the first reservation open while asking the OS for the second port.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as runtime_sock:
        runtime_sock.bind(("127.0.0.1", 0))
        runtime_port = runtime_sock.getsockname()[1]
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as engine_sock:
            engine_sock.bind(("127.0.0.1", 0))
            engine_port = engine_sock.getsockname()[1]
            if runtime_port == engine_port:
                raise RuntimeError("OS returned the same port for runtime and fake engine")
            return runtime_port, engine_port


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
    parser.add_argument("--fake-engine-script", required=True)
    parser.add_argument("--web-dir", required=True)
    args = parser.parse_args()

    runtime = str(Path(args.runtime).resolve())
    fake_script = str(Path(args.fake_engine_script).resolve())
    web_dir = str(Path(args.web_dir).resolve())
    for path in (runtime, fake_script, web_dir):
        if not Path(path).exists():
            raise RuntimeError(f"Required smoke-test path does not exist: {path}")

    runtime_port, engine_port = reserve_distinct_ports()
    with tempfile.TemporaryDirectory(prefix="lar-http-smoke-") as temporary:
        temp = Path(temporary)
        config_path = temp / "engines.json"
        config = {
            "server": {"host": "127.0.0.1", "port": runtime_port},
            "engines": [{
                "id": "fake-engine",
                "display_name": "CI fake OpenAI-compatible engine",
                "executable_path": sys.executable,
                "model_path": "",
                "working_directory": str(temp),
                "args": ["-u", fake_script, "--port", str(engine_port)],
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
        process = subprocess.Popen(
            [runtime, "--config", str(config_path), "--web-dir", web_dir],
            cwd=str(temp), stdout=runtime_log, stderr=subprocess.STDOUT,
        )
        try:
            wait_until(lambda: request(base + "/health")[0] == 200, 15, "runtime health endpoint")
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

            status, version_raw, content_type = request(base + "/version")
            version_info = json.loads(version_raw)
            assert status == 200 and "application/json" in content_type
            assert version_info["version"] and version_info["repository"].endswith("/local-ai-runtime")
            assert "releases_api_url" in version_info and "api.github.com" in version_info["releases_api_url"]

            status, engines_raw, _ = request(base + "/v1/engines")
            engines = json.loads(engines_raw)["data"]
            assert status == 200 and len(engines) == 1 and engines[0]["status"] == "stopped"

            request(base + "/v1/engines/fake-engine/start", method="POST", body={})

            # The start endpoint confirms process creation, not application
            # readiness. Wait for the upstream API itself before testing proxying.
            wait_until(
                lambda: request(engine_base + "/v1/models")[0] == 200,
                10,
                "fake engine readiness",
            )

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
            print("HTTP smoke test passed: dashboard, health/version metadata, Host/Origin guards, engine lifecycle, models proxy, chat proxy.")
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
            engine_log_path = temp / "logs" / "fake-engine.log"
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
