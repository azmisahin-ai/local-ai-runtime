#!/usr/bin/env python3
"""End-to-end task API smoke: Local AI Runtime -> typed audio.cpp adapter."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def reserve_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def request(url: str, method: str = "GET", body: bytes | dict | None = None,
            headers: dict[str, str] | None = None, timeout: float = 3.0) -> tuple[int, bytes, str]:
    if isinstance(body, dict):
        data = json.dumps(body).encode("utf-8")
    else:
        data = body
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read(), response.headers.get("Content-Type", "")
    except urllib.error.HTTPError as error:
        return error.code, error.read(), error.headers.get("Content-Type", "")


def wait_until(predicate, timeout: float, message: str) -> None:
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except Exception as error:  # diagnostic only; polling continues
            last_error = error
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {message}; last error: {last_error}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--fake-engine", required=True)
    args = parser.parse_args()
    runtime = str(Path(args.runtime).resolve())
    fake = Path(args.fake_engine).resolve()
    with tempfile.TemporaryDirectory(prefix="lar-task-http-smoke-") as temporary:
        temp = Path(temporary)
        runtime_port, engine_port = reserve_port(), reserve_port()
        if runtime_port == engine_port:
            engine_port = reserve_port()
        server_name = "audiocpp_server.exe" if os.name == "nt" else "audiocpp_server"
        server_path = temp / server_name
        shutil.copy2(fake, server_path)
        if os.name != "nt":
            server_path.chmod(server_path.stat().st_mode | 0o111)
        config_path = temp / "engines.json"
        config = {
            "server": {"host": "127.0.0.1", "port": runtime_port},
            "engines": [{
                "id": "audio-cpp",
                "adapter_id": "audio.cpp",
                "display_name": "Fake audio.cpp task server",
                "executable_path": str(server_path),
                "working_directory": str(temp),
                "args": ["--port", str(engine_port)],
                "upstream_base_url": f"http://127.0.0.1:{engine_port}",
                "capabilities": ["asr.transcribe", "tts.synthesize"],
                "default_for_chat": False,
            }],
        }
        config_path.write_text(json.dumps(config), encoding="utf-8")
        data_root = temp / "data"
        env = os.environ.copy()
        env["LOCAL_AI_RUNTIME_DATA_DIR"] = str(data_root)
        process = subprocess.Popen(
            [runtime, "--config", str(config_path)], cwd=temp, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        try:
            base = f"http://127.0.0.1:{runtime_port}"
            wait_until(lambda: request(base + "/health")[0] == 200, 15, "runtime health")
            status, body, _ = request(base + "/v1/engines/audio-cpp/start", method="POST", body={})
            assert status == 200, body.decode("utf-8", errors="replace")
            wait_until(lambda: any(
                engine.get("id") == "audio-cpp" and engine.get("ready") is True
                for engine in json.loads(request(base + "/v1/engines")[1]).get("data", [])
            ), 10, "audio.cpp readiness")

            wav = b"RIFF" + b"\x00\x00\x00\x00" + b"WAVE" + b"fmt " + b"\x10\x00\x00\x00" + b"data"
            wav = wav[:4] + (len(wav) - 8).to_bytes(4, "little") + wav[8:]
            status, body, content_type = request(
                base + "/v1/tasks/asr/transcribe", method="POST", body=wav,
                headers={"Content-Type": "audio/wav", "X-LAR-Model": "qwen3-asr", "X-LAR-Language": "tr"},
            )
            result = json.loads(body)
            assert status == 200 and result.get("task") == "asr.transcribe", result
            assert result.get("engine") == "audio.cpp" and result.get("model_id") == "qwen3-asr", result
            assert result.get("language") == "tr" and result.get("text") == "Merhaba dünya", result
            assert "application/json" in content_type

            status, body, content_type = request(
                base + "/v1/tasks/asr/transcribe", method="POST", body=wav,
                headers={"Content-Type": "audio/wav", "X-LAR-Model": "../unsafe", "X-LAR-Language": "tr"},
            )
            assert status == 400 and b"model_id" in body, body

            status, body, content_type = request(
                base + "/v1/tasks/tts/synthesize", method="POST",
                body={"model_id": "test-tts", "text": "Merhaba dünya", "voice": "voice_1"},
                headers={"Content-Type": "application/json"},
            )
            assert status == 200 and content_type.startswith("audio/wav"), (status, content_type, body[:80])
            assert body.startswith(b"RIFF") and body[8:12] == b"WAVE", body[:16]

            # Audio runtimes must never become the default Chat engine.
            status, body, _ = request(
                base + "/v1/chat/completions", method="POST",
                body={"model": "unused", "messages": [{"role": "user", "content": "test"}]},
                headers={"Content-Type": "application/json"},
            )
            assert status == 503, (status, body)
            status, body, _ = request(base + "/v1/engines/audio-cpp/stop", method="POST", body={})
            assert status == 200, body
            print("Typed audio task HTTP smoke passed: ASR multipart/tr, invalid model token rejection, TTS WAV artifact, Chat isolation and stop.")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            if process.stdout:
                output = process.stdout.read()
                if process.returncode not in (0, -15, 1) and output:
                    raise AssertionError(f"Runtime exited unexpectedly ({process.returncode}):\n{output[-5000:]}")


if __name__ == "__main__":
    main()
