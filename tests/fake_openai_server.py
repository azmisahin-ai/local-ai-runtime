#!/usr/bin/env python3
"""Tiny stdlib-only OpenAI-compatible engine used exclusively by CI smoke tests."""
import argparse
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def _send_json(self, status, body):
        payload = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/v1/models":
            self._send_json(200, {"object": "list", "data": [{"id": "fake-model", "object": "model"}]})
        else:
            self._send_json(404, {"error": {"message": "not found"}})

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self._send_json(404, {"error": {"message": "not found"}})
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length:
            self.rfile.read(length)
        self._send_json(200, {
            "id": "chatcmpl-ci-smoke",
            "object": "chat.completion",
            "choices": [{
                "index": 0,
                "message": {"role": "assistant", "content": "LAR_HTTP_SMOKE_OK"},
                "finish_reason": "stop"
            }]
        })

    def log_message(self, _format, *_args):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"FAKE_ENGINE_READY host=127.0.0.1 port={args.port} cwd={os.getcwd()}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
